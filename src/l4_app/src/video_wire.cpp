// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_wire.h"
#include "hal/selected.h"
#include "proto/spi_frame.h"

#include <cstring>

#include "svc/audio_service.h"
#include "svc/but_sw_wire.h"
#include "app/input_wire.h"

namespace mister::app {

Ex<void> VideoWire::publish(proto::UioOp opcode, std::span<const std::uint16_t> words) {
    return stage(opcode, words);
}

Ex<void> VideoWire::stage(proto::UioOp opcode, std::span<const std::uint16_t> words) {
    if (words.empty() || words.size() > kWords) {
        staged_rejected_.add();
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size())}
        );
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

        staged_rejected_.add();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), gen});
    }
    gen_ = gen;
    staged_.add();
    return {};
}

Ex<void> VideoWire::emit(
    hal::ISpiTransport& link,
    std::uint8_t slot,
    std::uint32_t gen,
    infra::OptRef<proto::IResetFence> fence
) {

    if (prelude_running() && !pre_replaying_) {
        pre_parked_slot_ = slot;
        pre_parked_gen_ = gen;
        pre_parked_ = true;
        ++pre_.parked;
        publish_prelude();
        return {};
    }
    if (slot >= kSlots) {
        emit_rejected_.add();
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot});
    }

    BlockPayload local{};
    if (gen == 0u
        || table_[slot].try_read(local) != gen
        || local.count == 0u
        || local.count > kWords) {
        stale_.add();
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), gen});
    }
    const std::uint8_t count = local.count;
    proto::SpiFrame frame{link};
    const auto r = svc::emit_video_words(
        frame,
        local.opcode,
        std::span<const std::uint16_t>(local.words, count)
    );
    if (!r) {

        partial_.add();
        return r;
    }
    emitted_.add();

    if (!pre_in_block_) {
        if (auto arm = emit_but_sw_(link, tail_but_sw_, fence); !arm) {

            arm_fail_.add();
        }
    }
    return {};
}

bool VideoWire::update_but_sw(
    hal::ISpiTransport& link,
    std::uint16_t buttons,
    bool osd_visible,
    infra::OptRef<proto::IResetFence> fence
) noexcept {

    auto map = static_cast<std::uint16_t>(tail_but_sw_ & ~(kButton1 | kButton2));
    if ((buttons & app::InputWire::kButtonOsd) != 0u)
        map |= kButton1;
    if ((buttons & app::InputWire::kButtonUser) != 0u)
        map |= kButton2;

    const std::uint16_t composed = map;
    if (but_sw_seen_ && composed == but_sw_shadow_)
        return false;

    std::uint16_t wire = composed;
    if (osd_visible)
        wire = static_cast<std::uint16_t>(wire & ~kButton2);
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

Ex<void> VideoWire::emit_but_sw_(
    hal::ISpiTransport& link,
    std::uint16_t word,
    infra::OptRef<proto::IResetFence> fence
) noexcept {
    if (fence)
        fence->before_buttons(but_sw_sent_, word);
    proto::SpiFrame frame{link};
    auto r = svc::emit_but_sw(frame, word);
    if (r)
        but_sw_sent_ = word;
    return r;
}

VideoWire::Stats VideoWire::stats() const noexcept {
    Stats s{};
    s.staged = staged_.get();
    s.emitted = emitted_.get();
    s.stale = stale_.get();
    s.partial = partial_.get();
    s.staged_rejected = staged_rejected_.get();
    s.emit_rejected = emit_rejected_.get();
    s.rejected = s.staged_rejected + s.emit_rejected;
    s.arm_fail = arm_fail_.get();
    return s;
}

namespace {
constexpr std::int64_t kMs = 1'000'000;
}

void VideoWire::on_rt_round(
    hal::ISpiTransport& link,
    std::int64_t now_ns,
    infra::OptRef<proto::IResetFence> fence
) {
    geo_rounds_.add();

    if (auto v = policy_seen_.take_if_changed(policy_))
        policy_bits_ = *v;
    if (auto v = ar_cust_seen_.take_if_changed(ar_cust_))
        ar_cust_bits_ = *v;
    if (auto v = shadow_seen_.take_if_changed(shadow_mask_))
        shadow_mask_word_ = *v;

    if (step_prelude(link, fence))
        return;

    if (fs_active_) {
        step_filter_stream(link);
        return;
    }

    if (service_filter_publish(link))
        return;

    if (geo_next_ns_ == 0) {
        geo_next_ns_ = now_ns + kGeoFirstPeriodMs * kMs;
        return;
    }
    if (now_ns < geo_next_ns_)
        return;

    geo_next_ns_ += kGeoPeriodMs * kMs;
    if (geo_next_ns_ <= now_ns) {
        geo_next_ns_ = now_ns + kGeoPeriodMs * kMs;
        geo_late_.add();
    }

    bool force = force_seen_.take(force_);

    if (geo_force_next_) {
        geo_force_next_ = false;
        force = true;
    }

    geo_gate_.policy = policy();
    auto pass = svc::sample_video_geometry(link, force, geo_gate_, this);
    if (!pass) {
        geo_errors_.add();
        return;
    }
    geo_samples_.add();
    if (pass->changed)
        geo_changes_.add();
    if (pass->resent)
        geo_resends_.add();

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
    geo_reads_.add();
    if (force)
        request_force();

    if (const auto s = geo_cell_.sample()) {
        sampled_seq_ = s.value.core_seq;
        return s.value.sample;
    }

    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

VideoWire::GeoStats VideoWire::geo_stats() const noexcept {
    GeoStats s;
    s.rounds = geo_rounds_.get();
    s.samples = geo_samples_.get();
    s.changes = geo_changes_.get();
    s.resends = geo_resends_.get();
    s.errors = geo_errors_.get();
    s.late = geo_late_.get();
    s.generation = geo_cell_.generation();
    s.reads = geo_reads_.get();
    s.torn = geo_cell_.refusals();
    return s;
}

void VideoWire::arm_prelude() noexcept {
    pre_step_ = PreludeStep::Fbuf;
    pre_barrier_ = 0;
    pre_flt_barrier_ = 0;

    pre_parked_ = false;
    pre_.hdmi_int = 0;
    pre_.gamma_cap = 0;
    pre_.af_flags = 0;
    pre_.tail_fail = 0;

    fs_active_ = false;
    fs_tail_ = false;
    fs_prelude_ = false;
    flt_have_ = false;
    flt_seen_gen_ = flt_cell_.generation();
    sent_horiz_ = svc::FilterBank{};
    sent_vert_ = svc::FilterBank{};
    ++pre_.arms;
    publish_prelude();
}

bool VideoWire::step_prelude(hal::ISpiTransport& link, infra::OptRef<proto::IResetFence> fence) {
    if (!prelude_running())
        return false;

    switch (pre_step_) {
        case PreludeStep::Fbuf: {

            proto::SpiFrame frame{link};
            auto r = svc::emit_fbuf(frame, 0x0000);
            if (!r) {
                ++pre_.errors;
            } else {
                pre_.fb_ack = static_cast<std::uint16_t>(*r);
            }

            {
                auto hi = svc::emit_hdmi_int_probe(link);
                if (!hi)
                    ++pre_.errors;
                pre_.hdmi_int = static_cast<std::uint16_t>(hi ? *hi : 0u);
                auto gm = svc::emit_gamma_probe(link);
                if (!gm)
                    ++pre_.errors;
                pre_.gamma_cap = static_cast<std::uint16_t>(gm ? *gm : 0u);
            }

            {
                const std::uint64_t bits = ar_cust_bits_;
                const std::uint16_t arc[4] = {
                    static_cast<std::uint16_t>(bits & 0xFFFFu),
                    static_cast<std::uint16_t>((bits >> 16) & 0xFFFFu),
                    static_cast<std::uint16_t>((bits >> 32) & 0xFFFFu),
                    static_cast<std::uint16_t>((bits >> 48) & 0xFFFFu)
                };
                auto a = svc::emit_ar_custom(frame, std::span<const std::uint16_t>(arc, 4));
                if (!a)
                    ++pre_.errors;
            }
            pre_step_ = PreludeStep::FltNum;
            break;
        }

        case PreludeStep::FltNum: {

            const std::uint32_t g = flt_cell_.generation();
            if (g == 0u || g == flt_seen_gen_) {
                if (pre_flt_barrier_ < kBarrierRounds) {
                    ++pre_flt_barrier_;
                    flt_waits_.add();
                    return true;
                }
                flt_wait_timeouts_.add();
            } else if (!consume_filters()) {
                return true;
            }

            const std::uint8_t mode = flt_have_ ? flt_local_.modes[0] : policy().filter_mode;
            auto f = svc::emit_filter_mode(link, mode);
            if (!f) {

                ++pre_.errors;
                pre_step_ = PreludeStep::Burst;
                break;
            }
            pre_.flt_flags = static_cast<std::uint16_t>(*f);

            if (*f != 0)
                geo_gate_.last_flt_flags = *f;
            if (*f == 0) {
                ++pre_.coeff_skips;
                pre_step_ = PreludeStep::Burst;
                break;
            }
            if (flt_have_) {
                svc::VideoSample s{};
                s.flt_flags = *f;
                if (!arm_filter_stream(*f, select_slot(s), true, false, svc::ScalingWords{})) {

                    proto::SpiFrame frame{link};
                    if (auto e = svc::emit_filter_coeff_chunk(frame, {}); !e)
                        ++pre_.errors;
                    ++pre_.chunks;
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
            if (!fs_active_)
                pre_step_ = PreludeStep::Burst;
            break;
        }

        case PreludeStep::Burst: {

            if (!pre_parked_) {
                ++pre_barrier_;
                ++pre_.barrier_waits;
                if (pre_barrier_ < kBarrierRounds) {
                    publish_prelude();
                    return true;
                }

                ++pre_.barrier_timeouts;
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
                static_cast<std::uint16_t>(shadow_mask_word_.v & 0xFFFFu)
            );
            if (!r) {
                ++pre_.errors;
            } else {
                pre_.sm_cap = static_cast<std::uint16_t>(*r);
            }

            pre_step_ = PreludeStep::Tail;
            break;
        }

        case PreludeStep::Tail: {

            std::uint16_t tf = 0;
            std::uint16_t af = 0;
            if (auto p = svc::emit_afilter_probe(link); !p) {
                ++pre_.errors;
                tf |= static_cast<std::uint16_t>(kTailFailProbe);
            } else {
                af = *p;
            }
            pre_.af_flags = af;

            audio_.accept_probe_reply(static_cast<std::uint8_t>(af), policy().front_end);
            if (auto f = audio_.flush(); !f) {
                ++pre_.errors;
                tf |= static_cast<std::uint16_t>(kTailFailFlush);
            }
            const std::uint8_t push_vol = audio_.core_volume_byte();
            const std::uint8_t audvol = audio_.audvol_wire_byte();
            proto::SpiFrame frame{link};

            if (af != 0) {
                if (auto r = svc::emit_afilter_volume(frame, push_vol); !r) {
                    ++pre_.errors;
                    tf |= static_cast<std::uint16_t>(kTailFailPush);
                }
            } else {
                ++pre_.afilter_skips;
            }

            if (auto r = svc::emit_audvol(frame, audvol); !r) {
                ++pre_.errors;
                tf |= static_cast<std::uint16_t>(kTailFailVol);
            }
            if (auto r = emit_but_sw_(link, tail_but_sw_, fence); !r) {
                ++pre_.errors;
                tf |= static_cast<std::uint16_t>(kTailFailButSw);
            }
            pre_.tail_fail = tf;

            pre_step_ = PreludeStep::Done;

            ++pre_.completions;

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

    ++pre_.steps;
    publish_prelude();
    return true;
}

void VideoWire::publish_filters(const svc::FilterSet& s) noexcept {
    flt_cell_.publish(s);
    flt_published_.add();
}

bool VideoWire::consume_filters() {
    const std::uint32_t g = flt_cell_.sample_into(flt_local_);
    if (g == 0u)
        return false;
    flt_seen_gen_ = g;
    flt_have_ = true;
    flt_consumed_.add();
    return true;
}

std::size_t VideoWire::select_slot(const svc::VideoSample& s) const {
    const bool enabled[4] = {
        flt_local_.modes[0] != 0,
        flt_local_.modes[1] != 0,
        flt_local_.modes[2] != 0,
        flt_local_.modes[3] != 0
    };
    return static_cast<std::size_t>(
        svc::video_timing::select_vfilter(s, std::span<const bool>(enabled, 4))
    );
}

bool VideoWire::service_filter_publish(hal::ISpiTransport& link) {
    const std::uint32_t g = flt_cell_.generation();
    if (g == 0u || g == flt_seen_gen_)
        return false;
    if (!consume_filters())
        return false;

    auto f = svc::emit_filter_mode(link, flt_local_.modes[0]);
    if (!f) {
        flt_stream_errors_.add();
        return true;
    }
    if (*f == 0)
        return true;
    geo_gate_.last_flt_flags = *f;
    svc::VideoSample s = geo_gate_.last;
    s.flt_flags = *f;
    if (!arm_filter_stream(*f, select_slot(s), false, false, svc::ScalingWords{})) {

        proto::SpiFrame frame{link};
        if (auto e = svc::emit_filter_coeff_chunk(frame, {}); !e)
            flt_stream_errors_.add();
    }
    return true;
}

bool VideoWire::arm_filter_stream(
    std::uint16_t flt_flags,
    std::size_t vert_slot,
    bool prelude,
    bool tail,
    const svc::ScalingWords& tail_words
) {
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
    flt_streams_.add();
    return true;
}

bool VideoWire::plan_filter_send(const svc::VideoSample& s, bool resend) {

    if (!flt_have_)
        return false;
    svc::ScalingWords tw{};
    if (resend)
        tw = svc::scaling_words(s, geo_gate_.policy);
    return arm_filter_stream(s.flt_flags, select_slot(s), false, resend, tw);
}

void VideoWire::step_filter_stream(hal::ISpiTransport& link) {
    proto::SpiFrame frame{link};
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
            buf[i] = svc::filter_bank_word(
                std::span<const svc::FilterPhase>(ph),
                fs_ver_,
                seg.bank,
                fs_cursor_ + i
            );
        }
    }

    if (auto r = svc::emit_filter_coeff_chunk(frame, std::span<const std::uint16_t>(buf, n)); !r) {

        flt_stream_errors_.add();
        if (fs_prelude_)
            ++pre_.errors;
        fs_active_ = false;
        fs_tail_ = false;
        return;
    }
    if (fs_prelude_) {
        ++pre_.chunks;
        pre_.coeff_words += static_cast<std::uint32_t>(n);
    } else {
        flt_stream_words_.add(static_cast<std::uint32_t>(n));
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
    if (!done)
        return;

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
            if (auto r = link.transfer(proto::word(proto::UioOp::SetHeight)); !r) {
                flt_stream_errors_.add();
                return;
            }
            if (auto r = link.transfer(hal::SpiWord{fs_tail_words_.height}); !r) {
                flt_stream_errors_.add();
                return;
            }
        }
        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(proto::word(proto::UioOp::SetWidth)); !r) {
                flt_stream_errors_.add();
                return;
            }
            if (auto r = link.transfer(hal::SpiWord{fs_tail_words_.width}); !r) {
                flt_stream_errors_.add();
                return;
            }
        }
    }
}

VideoWire::FilterStats VideoWire::filter_stats() const noexcept {
    FilterStats s;
    s.published = flt_published_.get();
    s.consumed = flt_consumed_.get();
    s.waits = flt_waits_.get();
    s.wait_timeouts = flt_wait_timeouts_.get();
    s.streams = flt_streams_.get();
    s.stream_words = flt_stream_words_.get();
    s.stream_errors = flt_stream_errors_.get();
    return s;
}

void VideoWire::publish_prelude() noexcept {
    pre_.step = static_cast<std::uint8_t>(pre_step_);
    prelude_cell_.publish(pre_);
}

PreludeStats VideoWire::prelude_stats() const noexcept {
    return prelude_cell_.sample().value;
}

}
