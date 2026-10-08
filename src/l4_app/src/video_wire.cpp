// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_wire.h"
#include "hal/selected.h"

#include <atomic>
#include <cstring>

#include "svc/audio_service.h"
#include "svc/input_service.h"
#include "app/input_wire.h"

namespace mister::app {

Ex<void> VideoWire::publish(std::uint8_t opcode, std::span<const std::uint16_t> words) {
    return stage(opcode, words);
}

Ex<void> VideoWire::stage(std::uint8_t opcode, std::span<const std::uint16_t> words) {
    if (words.empty() || words.size() > kWords) {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size())});
    }

    const std::uint32_t gen = (gen_ + 1u) != 0u ? gen_ + 1u : 1u;
    const std::size_t slot = static_cast<std::size_t>(gen) % kSlots;

    BlockPayload p{};
    std::memcpy(p.words, words.data(), words.size() * sizeof(std::uint16_t));
    p.opcode = opcode;
    p.count = static_cast<std::uint8_t>(words.size());
    table_[slot].write(gen, p);

    const proto::LinkOp::SetVideoMode mode{.block = static_cast<std::uint8_t>(slot), .gen = gen};
    if (!link_tx_.push(mode)) {

        rejected_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), gen});
    }
    gen_ = gen;
    staged_.fetch_add(1, std::memory_order_relaxed);
    return {};
}

Ex<void> VideoWire::emit(hal::ISpiTransport& link, std::uint8_t slot, std::uint32_t gen,
                         proto::IResetFence* fence) {

    if (prelude_running() && !pre_replaying_) {
        pre_parked_slot_ = slot;
        pre_parked_gen_ = gen;
        pre_parked_ = true;
        bump(pre_parked_n_);
        return {};
    }
    if (slot >= kSlots) {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot});
    }

    BlockPayload local{};
    if (gen == 0u || table_[slot].try_read(local) != gen || local.count == 0u ||
        local.count > kWords) {
        stale_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), gen});
    }
    const std::uint8_t opcode = local.opcode;
    const std::uint8_t count = local.count;
    const auto r =
        svc::emit_video_words(link, opcode, std::span<const std::uint16_t>(local.words, count));
    if (!r) {

        partial_.fetch_add(1, std::memory_order_relaxed);
        return r;
    }
    emitted_.fetch_add(1, std::memory_order_relaxed);

    if (!pre_in_block_) {
        if (auto arm = emit_but_sw_(link, tail_but_sw_, fence); !arm) {

            bump(arm_fail_);
        }
    }
    return {};
}

bool VideoWire::update_but_sw(hal::ISpiTransport& link, std::uint16_t buttons, bool osd_visible,
                              proto::IResetFence* fence) noexcept {

    auto map = static_cast<std::uint16_t>(tail_but_sw_ & ~(kButton1 | kButton2));
    if ((buttons & app::InputWire::kButtonOsd) != 0u) map |= kButton1;
    if ((buttons & app::InputWire::kButtonUser) != 0u) map |= kButton2;

    const std::uint16_t composed = map;
    if (but_sw_seen_ && composed == but_sw_shadow_) return false;

    std::uint16_t wire = composed;
    if (osd_visible) wire = static_cast<std::uint16_t>(wire & ~kButton2);
    tail_but_sw_ = wire;
    ++but_sw_writes_;
    if (auto r = emit_but_sw_(link, wire, fence); !r) {
        ++but_sw_errors_;
        return false;
    }

    but_sw_shadow_ = composed;
    but_sw_seen_ = true;
    return true;
}

Ex<void> VideoWire::emit_but_sw_(hal::ISpiTransport& link, std::uint16_t word,
                                 proto::IResetFence* fence) noexcept {
    if (fence != nullptr) fence->before_buttons(but_sw_sent_, word);
    auto r = svc::emit_but_sw(link, word);
    if (r) but_sw_sent_ = word;
    return r;
}

VideoWire::Stats VideoWire::stats() const noexcept {
    Stats s{};
    s.staged = staged_.load(std::memory_order_relaxed);
    s.emitted = emitted_.load(std::memory_order_relaxed);
    s.stale = stale_.load(std::memory_order_relaxed);
    s.partial = partial_.load(std::memory_order_relaxed);
    s.rejected = rejected_.load(std::memory_order_relaxed);
    s.arm_fail = arm_fail_.load(std::memory_order_relaxed);
    return s;
}

namespace {
constexpr std::int64_t kMs = 1'000'000;
}

void VideoWire::on_rt_round(hal::ISpiTransport& link, std::int64_t now_ns,
                            proto::IResetFence* fence) {
    bump(geo_rounds_);

    if (step_prelude(link, fence)) return;

    if (fs_active_) {
        step_filter_stream(link);
        return;
    }

    if (service_filter_publish(link)) return;

    if (geo_next_ns_ == 0) {
        geo_next_ns_ = now_ns + kGeoFirstPeriodMs * kMs;
        return;
    }
    if (now_ns < geo_next_ns_) return;

    geo_next_ns_ += kGeoPeriodMs * kMs;
    if (geo_next_ns_ <= now_ns) {
        geo_next_ns_ = now_ns + kGeoPeriodMs * kMs;
        bump(geo_late_);
    }

    const std::uint32_t fseq = force_seq_.load(std::memory_order_acquire);
    bool force = false;
    if (fseq != force_seen_) {
        force_seen_ = fseq;
        force = true;
    }

    if (geo_force_next_) {
        geo_force_next_ = false;
        force = true;
    }

    geo_gate_.policy = policy();
    auto pass = svc::sample_video_geometry(link, force, geo_gate_, this);
    if (!pass) {
        bump(geo_errors_);
        return;
    }
    bump(geo_samples_);
    if (pass->changed) bump(geo_changes_);
    if (pass->resent) bump(geo_resends_);

    geo_cell_.publish(VideoGeoLatch{.sample = pass->sample, .core_seq = geo_core_seq_});
}

void VideoWire::reset_geometry(std::uint32_t core_seq) noexcept {
    geo_core_seq_ = core_seq;
    geo_gate_ = svc::GeometryGate{};

    geo_force_next_ = true;

    geo_next_ns_ = 0;

    geo_cell_.invalidate();
}

Ex<svc::VideoSample> VideoWire::sample(bool force) {
    bump(geo_reads_);
    if (force) request_force();

    if (const auto s = geo_cell_.sample()) {
        sampled_seq_ = s.value.core_seq;
        return s.value.sample;
    }

    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

VideoWire::GeoStats VideoWire::geo_stats() const noexcept {
    GeoStats s;
    s.rounds = geo_rounds_.load(std::memory_order_relaxed);
    s.samples = geo_samples_.load(std::memory_order_relaxed);
    s.changes = geo_changes_.load(std::memory_order_relaxed);
    s.resends = geo_resends_.load(std::memory_order_relaxed);
    s.errors = geo_errors_.load(std::memory_order_relaxed);
    s.late = geo_late_.load(std::memory_order_relaxed);
    s.generation = geo_cell_.generation();
    s.reads = geo_reads_.load(std::memory_order_relaxed);
    s.torn = geo_cell_.refusals();
    return s;
}

void VideoWire::arm_prelude() noexcept {
    pre_step_ = PreludeStep::Fbuf;
    pre_barrier_ = 0;
    pre_flt_barrier_ = 0;

    pre_parked_ = false;
    pre_hdmi_int_.store(0, std::memory_order_relaxed);
    pre_gamma_cap_.store(0, std::memory_order_relaxed);
    pre_af_flags_.store(0, std::memory_order_relaxed);
    pre_tail_fail_.store(0, std::memory_order_relaxed);

    fs_active_ = false;
    fs_tail_ = false;
    fs_prelude_ = false;
    flt_have_ = false;
    flt_seen_gen_ = flt_cell_.generation();
    sent_horiz_ = svc::FilterBank{};
    sent_vert_ = svc::FilterBank{};
    bump(pre_arms_);
}

bool VideoWire::step_prelude(hal::ISpiTransport& link, proto::IResetFence* fence) {
    if (!prelude_running()) return false;

    switch (pre_step_) {
        case PreludeStep::Fbuf: {

            auto r = svc::emit_fbuf(link, 0x0000);
            if (!r) {
                bump(pre_errors_);
            } else {
                pre_fb_ack_.store(*r, std::memory_order_relaxed);
            }

            {
                auto hi = svc::emit_hdmi_int_probe(link);
                if (!hi) bump(pre_errors_);
                pre_hdmi_int_.store(hi ? *hi : 0u, std::memory_order_relaxed);
                auto gm = svc::emit_gamma_probe(link);
                if (!gm) bump(pre_errors_);
                pre_gamma_cap_.store(gm ? *gm : 0u, std::memory_order_relaxed);
            }

            {
                const std::uint64_t bits = ar_cust_.load(std::memory_order_acquire);
                const std::uint16_t arc[4] = {static_cast<std::uint16_t>(bits & 0xFFFFu),
                                              static_cast<std::uint16_t>((bits >> 16) & 0xFFFFu),
                                              static_cast<std::uint16_t>((bits >> 32) & 0xFFFFu),
                                              static_cast<std::uint16_t>((bits >> 48) & 0xFFFFu)};
                auto a = svc::emit_ar_custom(link, std::span<const std::uint16_t>(arc, 4));
                if (!a) bump(pre_errors_);
            }
            pre_step_ = PreludeStep::FltNum;
            break;
        }

        case PreludeStep::FltNum: {

            const std::uint32_t g = flt_cell_.generation();
            if (g == 0u || g == flt_seen_gen_) {
                if (pre_flt_barrier_ < kBarrierRounds) {
                    ++pre_flt_barrier_;
                    bump(flt_waits_);
                    return true;
                }
                bump(flt_wait_timeouts_);
            } else if (!consume_filters()) {
                return true;
            }

            const std::uint8_t mode = flt_have_ ? flt_local_.modes[0] : policy().filter_mode;
            auto f = svc::emit_filter_mode(link, mode);
            if (!f) {

                bump(pre_errors_);
                pre_step_ = PreludeStep::Burst;
                break;
            }
            pre_flt_flags_.store(*f, std::memory_order_relaxed);

            if (*f != 0) geo_gate_.last_flt_flags = *f;
            if (*f == 0) {
                bump(pre_coeff_skips_);
                pre_step_ = PreludeStep::Burst;
                break;
            }
            if (flt_have_) {
                svc::VideoSample s{};
                s.flt_flags = *f;
                if (!arm_filter_stream(*f, select_slot(s), true, false, svc::ScalingWords{})) {

                    if (auto e = svc::emit_filter_coeff_chunk(link, {}); !e) bump(pre_errors_);
                    bump(pre_chunks_);
                    pre_step_ = PreludeStep::Burst;
                    break;
                }
            } else {

                fs_generated_ = true;
                fs_prelude_ = true;
                fs_ver_ = static_cast<std::uint16_t>(*f & 0xFu);
                fs_cursor_ = 0;
                fs_tail_ = false;
                fs_active_ = true;
            }
            pre_step_ = PreludeStep::FltCoef;
            break;
        }

        case PreludeStep::FltCoef: {

            step_filter_stream(link);
            if (!fs_active_) pre_step_ = PreludeStep::Burst;
            break;
        }

        case PreludeStep::Burst: {

            if (!pre_parked_) {
                ++pre_barrier_;
                bump(pre_barrier_waits_);
                if (pre_barrier_ < kBarrierRounds) return true;

                bump(pre_barrier_timeouts_);
                pre_step_ = PreludeStep::ShadowMask;
                break;
            }
            const std::uint8_t slot = pre_parked_slot_;
            const std::uint32_t gen = pre_parked_gen_;
            pre_parked_ = false;
            pre_step_ = PreludeStep::ShadowMask;

            pre_replaying_ = true;
            pre_in_block_ = true;
            (void)emit(link, slot, gen, fence);
            pre_in_block_ = false;
            pre_replaying_ = false;
            break;
        }

        case PreludeStep::ShadowMask: {

            auto r = svc::emit_shadow_mask(
                link,
                static_cast<std::uint16_t>(shadow_mask_.load(std::memory_order_acquire) & 0xFFFFu));
            if (!r) {
                bump(pre_errors_);
            } else {
                pre_sm_cap_.store(*r, std::memory_order_relaxed);
            }

            pre_step_ = PreludeStep::Tail;
            break;
        }

        case PreludeStep::Tail: {

            std::uint16_t tf = 0;
            std::uint16_t af = 0;
            if (auto p = svc::emit_afilter_probe(link); !p) {
                bump(pre_errors_);
                tf |= static_cast<std::uint16_t>(kTailFailProbe);
            } else {
                af = *p;
            }
            pre_af_flags_.store(af, std::memory_order_relaxed);

            audio_.accept_probe_reply(static_cast<std::uint8_t>(af), policy().front_end);
            if (auto f = audio_.flush(); !f) {
                bump(pre_errors_);
                tf |= static_cast<std::uint16_t>(kTailFailFlush);
            }
            const std::uint8_t push_vol = audio_.core_volume_byte();
            const std::uint8_t audvol = audio_.audvol_wire_byte();

            if (af != 0) {
                if (auto r = svc::emit_afilter_volume(link, push_vol); !r) {
                    bump(pre_errors_);
                    tf |= static_cast<std::uint16_t>(kTailFailPush);
                }
            } else {
                bump(pre_afilter_skips_);
            }

            if (auto r = svc::emit_audvol(link, audvol); !r) {
                bump(pre_errors_);
                tf |= static_cast<std::uint16_t>(kTailFailVol);
            }
            if (auto r = emit_but_sw_(link, tail_but_sw_, fence); !r) {
                bump(pre_errors_);
                tf |= static_cast<std::uint16_t>(kTailFailButSw);
            }
            pre_tail_fail_.store(tf, std::memory_order_relaxed);

            pre_step_ = PreludeStep::Done;

            bump(pre_completions_);

            if (pre_parked_) {
                const std::uint8_t slot = pre_parked_slot_;
                const std::uint32_t gen = pre_parked_gen_;
                pre_parked_ = false;
                pre_replaying_ = true;
                (void)emit(link, slot, gen, fence);
                pre_replaying_ = false;
            }
            break;
        }

        case PreludeStep::Idle:
        case PreludeStep::ArCust:
        case PreludeStep::Done:
            return false;
    }

    bump(pre_steps_);
    return true;
}

void VideoWire::publish_filters(const svc::FilterSet& s) noexcept {
    flt_cell_.publish(s);
    bump(flt_published_);
}

bool VideoWire::consume_filters() {
    const std::uint32_t g = flt_cell_.sample_into(flt_local_);
    if (g == 0u) return false;
    flt_seen_gen_ = g;
    flt_have_ = true;
    bump(flt_consumed_);
    return true;
}

std::size_t VideoWire::select_slot(const svc::VideoSample& s) const {
    const bool enabled[4] = {flt_local_.modes[0] != 0, flt_local_.modes[1] != 0,
                             flt_local_.modes[2] != 0, flt_local_.modes[3] != 0};
    return static_cast<std::size_t>(
        svc::VideoService::select_vfilter(s, std::span<const bool>(enabled, 4)));
}

bool VideoWire::service_filter_publish(hal::ISpiTransport& link) {
    const std::uint32_t g = flt_cell_.generation();
    if (g == 0u || g == flt_seen_gen_) return false;
    if (!consume_filters()) return false;

    auto f = svc::emit_filter_mode(link, flt_local_.modes[0]);
    if (!f) {
        bump(flt_stream_errors_);
        return true;
    }
    if (*f == 0) return true;
    geo_gate_.last_flt_flags = *f;
    svc::VideoSample s = geo_gate_.last;
    s.flt_flags = *f;
    if (!arm_filter_stream(*f, select_slot(s), false, false, svc::ScalingWords{})) {

        if (auto e = svc::emit_filter_coeff_chunk(link, {}); !e) bump(flt_stream_errors_);
    }
    return true;
}

bool VideoWire::arm_filter_stream(std::uint16_t flt_flags, std::size_t vert_slot, bool prelude,
                                  bool tail, const svc::ScalingWords& tail_words) {
    const auto ver = static_cast<std::uint16_t>(flt_flags & 0xFu);
    const svc::FilterBank& h = flt_local_.banks[0];
    const svc::FilterBank& v = flt_local_.banks[vert_slot];
    const bool send_h = !svc::same_digest(h, sent_horiz_);
    const bool send_v = !svc::same_digest(v, sent_vert_);
    const svc::FilterSendPlan plan = svc::plan_filter_banks(h, v, ver, send_h, send_v);
    if (plan.count == 0) {

        sent_horiz_ = h;
        sent_vert_ = v;
        return false;
    }
    fs_plan_ = plan;
    fs_ver_ = ver;
    fs_vert_slot_ = static_cast<std::uint8_t>(vert_slot);
    fs_seg_i_ = 0;
    fs_cursor_ = 0;
    fs_generated_ = false;
    fs_prelude_ = prelude;
    fs_tail_ = tail;
    fs_tail_words_ = tail_words;
    fs_active_ = true;
    bump(flt_streams_);
    return true;
}

bool VideoWire::plan_filter_send(const svc::VideoSample& s, bool resend) {

    if (!flt_have_) return false;
    svc::ScalingWords tw{};
    if (resend) tw = svc::scaling_words(s, geo_gate_.policy);
    return arm_filter_stream(s.flt_flags, select_slot(s), false, resend, tw);
}

void VideoWire::step_filter_stream(hal::ISpiTransport& link) {
    std::uint16_t buf[kCoeffChunkWords];
    std::size_t n = 0;
    const std::size_t total = fs_generated_ ? svc::filter_coeff_word_count(fs_ver_)
                                            : svc::filter_bank_word_count(fs_ver_);
    if (fs_generated_) {
        const std::size_t remaining = total - fs_cursor_;
        n = remaining < kCoeffChunkWords ? remaining : kCoeffChunkWords;
        for (std::size_t i = 0; i < n; ++i)
            buf[i] = svc::filter_coeff_word(fs_ver_, fs_cursor_ + i);
    } else {
        const svc::FilterSendPlan::Seg& seg = fs_plan_.segs[fs_seg_i_];
        const svc::FilterBank& b = seg.vert ? flt_local_.banks[fs_vert_slot_] : flt_local_.banks[0];
        const auto& ph = seg.adaptive ? b.adaptive : b.phases;
        const std::size_t remaining = total - fs_cursor_;
        n = remaining < kCoeffChunkWords ? remaining : kCoeffChunkWords;
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = svc::filter_bank_word(std::span<const svc::FilterPhase>(ph), fs_ver_, seg.bank,
                                           fs_cursor_ + i);
        }
    }

    if (auto r = svc::emit_filter_coeff_chunk(link, std::span<const std::uint16_t>(buf, n)); !r) {

        bump(flt_stream_errors_);
        if (fs_prelude_) bump(pre_errors_);
        fs_active_ = false;
        fs_tail_ = false;
        return;
    }
    if (fs_prelude_) {
        bump(pre_chunks_);
        pre_coeff_words_.store(pre_coeff_words_.load(std::memory_order_relaxed) +
                                   static_cast<std::uint32_t>(n),
                               std::memory_order_relaxed);
    } else {
        flt_stream_words_.store(flt_stream_words_.load(std::memory_order_relaxed) +
                                    static_cast<std::uint32_t>(n),
                                std::memory_order_relaxed);
    }

    fs_cursor_ += n;
    bool done = false;
    if (fs_cursor_ >= total) {
        if (fs_generated_) {
            done = true;
        } else {
            fs_cursor_ = 0;
            ++fs_seg_i_;
            done = fs_seg_i_ >= fs_plan_.count;
        }
    }
    if (!done) return;

    fs_active_ = false;

    if (fs_generated_) {
        sent_horiz_ = svc::nearest_neighbour_bank();
        sent_vert_ = sent_horiz_;
        fs_generated_ = false;
    } else {
        sent_horiz_ = flt_local_.banks[0];
        sent_vert_ = flt_local_.banks[fs_vert_slot_];
    }
    if (fs_tail_) {
        fs_tail_ = false;

        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(hal::SpiWord{svc::kUioSetHeight}); !r) {
                bump(flt_stream_errors_);
                return;
            }
            if (auto r = link.transfer(hal::SpiWord{fs_tail_words_.height}); !r) {
                bump(flt_stream_errors_);
                return;
            }
        }
        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(hal::SpiWord{svc::kUioSetWidth}); !r) {
                bump(flt_stream_errors_);
                return;
            }
            if (auto r = link.transfer(hal::SpiWord{fs_tail_words_.width}); !r) {
                bump(flt_stream_errors_);
                return;
            }
        }
    }
}

VideoWire::FilterStats VideoWire::filter_stats() const noexcept {
    FilterStats s;
    s.published = flt_published_.load(std::memory_order_relaxed);
    s.consumed = flt_consumed_.load(std::memory_order_relaxed);
    s.waits = flt_waits_.load(std::memory_order_relaxed);
    s.wait_timeouts = flt_wait_timeouts_.load(std::memory_order_relaxed);
    s.streams = flt_streams_.load(std::memory_order_relaxed);
    s.stream_words = flt_stream_words_.load(std::memory_order_relaxed);
    s.stream_errors = flt_stream_errors_.load(std::memory_order_relaxed);
    return s;
}

VideoWire::PreludeStats VideoWire::prelude_stats() const noexcept {
    PreludeStats s;
    s.arms = pre_arms_.load(std::memory_order_relaxed);
    s.completions = pre_completions_.load(std::memory_order_relaxed);
    s.steps = pre_steps_.load(std::memory_order_relaxed);
    s.chunks = pre_chunks_.load(std::memory_order_relaxed);
    s.coeff_words = pre_coeff_words_.load(std::memory_order_relaxed);
    s.parked = pre_parked_n_.load(std::memory_order_relaxed);
    s.barrier_waits = pre_barrier_waits_.load(std::memory_order_relaxed);
    s.barrier_timeouts = pre_barrier_timeouts_.load(std::memory_order_relaxed);
    s.errors = pre_errors_.load(std::memory_order_relaxed);
    s.coeff_skips = pre_coeff_skips_.load(std::memory_order_relaxed);
    s.afilter_skips = pre_afilter_skips_.load(std::memory_order_relaxed);
    s.flt_flags = static_cast<std::uint16_t>(pre_flt_flags_.load(std::memory_order_relaxed));
    s.fb_ack = static_cast<std::uint16_t>(pre_fb_ack_.load(std::memory_order_relaxed));
    s.sm_cap = static_cast<std::uint16_t>(pre_sm_cap_.load(std::memory_order_relaxed));
    s.hdmi_int = static_cast<std::uint16_t>(pre_hdmi_int_.load(std::memory_order_relaxed));
    s.gamma_cap = static_cast<std::uint16_t>(pre_gamma_cap_.load(std::memory_order_relaxed));
    s.af_flags = static_cast<std::uint16_t>(pre_af_flags_.load(std::memory_order_relaxed));
    s.tail_fail = static_cast<std::uint16_t>(pre_tail_fail_.load(std::memory_order_relaxed));
    s.step = static_cast<std::uint8_t>(pre_step_);
    return s;
}

}  // namespace mister::app
