// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_wire.h"

#include <cstring>

#include "svc/audio_service.h"
#include "svc/probe_ops.h"
#include "svc/but_sw_wire.h"
#include "hal/spi_sample.h"

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
    proto::Link& link,
    const proto::LinkOp::SetVideoMode& mode,
    infra::OptRef<proto::IResetFence> fence
) {

    if (prelude_running() && !pre_replaying_) {
        parked_ = mode;
        ++pre_.parked;
        publish_prelude();
        return {};
    }
    const std::size_t slot = mode.block;
    if (slot >= kSlots) {
        emit_rejected_.add();
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), mode.block});
    }

    BlockPayload local{};
    if (mode.gen == 0u
        || table_[slot].try_read(local) != mode.gen
        || local.count == 0u
        || local.count > kWords) {
        stale_.add();
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), mode.gen});
    }
    const std::uint8_t count = local.count;
    const auto r = svc::emit_video_words(
        link.frame(),
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
    proto::Link& link,
    hal::ButtonMask buttons,
    bool osd_visible,
    infra::OptRef<proto::IResetFence> fence
) noexcept {

    auto map = static_cast<std::uint16_t>(tail_but_sw_ & ~(kButton1 | kButton2));
    if (buttons.has(hal::SpiSample::kMenuButton))
        map |= kButton1;
    if (buttons.has(hal::SpiSample::kUserButton))
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
    proto::Link& link,
    std::uint16_t word,
    infra::OptRef<proto::IResetFence> fence
) noexcept {
    if (fence)
        fence->before_buttons(but_sw_sent_, word);
    auto r = svc::emit_but_sw(link.frame(), word);
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

void VideoWire::on_rt_round(
    proto::Link& link,
    std::int64_t now_ns,
    infra::OptRef<proto::IResetFence> fence
) {

    const os::RawMonotonicClock::time_point now{std::chrono::nanoseconds{now_ns}};
    svc::ProbeOps ops{link};
    geo_rounds_.add();

    if (auto v = policy_seen_.take_if_changed(policy_cell_))
        policy_ = *v;
    if (auto v = ar_cust_seen_.take_if_changed(ar_cust_))
        ar_cust_bits_ = *v;
    if (auto v = shadow_seen_.take_if_changed(shadow_mask_))
        shadow_mask_word_ = *v;

    if (step_prelude(link, fence))
        return;

    if (fs_.active()) {
        fs_.step(stream_ports_(), link);
        return;
    }

    if (service_filter_publish(link))
        return;

    if (!geo_next_) {
        geo_next_ = now + kGeoFirstPeriod;
        return;
    }
    if (now < *geo_next_)
        return;

    *geo_next_ += kGeoPeriod;
    if (*geo_next_ <= now) {
        *geo_next_ = now + kGeoPeriod;
        geo_late_.add();
    }

    bool force = force_seen_.take(force_);

    if (geo_force_next_) {
        geo_force_next_ = false;
        force = true;
    }

    geo_gate_.policy = policy();
    auto pass = ops.sample_video_geometry(force, geo_gate_, *this);
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

    geo_next_.reset();

    geo_cell_.invalidate();
}

Ex<svc::VideoSample> VideoWire::sample() {
    geo_reads_.add();

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

    parked_.reset();
    pre_.hdmi_int = 0;
    pre_.gamma_cap = 0;
    pre_.af_flags = 0;
    pre_.tail_fail = 0;

    fs_ = FilterStream{};
    flt_have_ = false;
    flt_seen_gen_ = flt_cell_.generation();
    sent_horiz_ = svc::FilterBank{};
    sent_vert_ = svc::FilterBank{};
    ++pre_.arms;
    publish_prelude();
}

void VideoWire::replay_parked_(
    proto::Link& link,
    infra::OptRef<proto::IResetFence> fence,
    bool with_but_sw
) noexcept {
    const proto::LinkOp::SetVideoMode mode = *parked_;
    parked_.reset();

    pre_replaying_ = true;
    pre_in_block_ = !with_but_sw;
    (void)emit(link, mode, fence);
    pre_in_block_ = false;
    pre_replaying_ = false;
}

bool VideoWire::step_prelude(proto::Link& link, infra::OptRef<proto::IResetFence> fence) {
    if (!prelude_running())
        return false;
    svc::ProbeOps ops{link};

    switch (pre_step_) {
        case PreludeStep::Fbuf: {

            auto r = svc::emit_fbuf(link.frame(), 0x0000);
            if (!r) {
                ++pre_.errors;
            } else {
                pre_.fb_ack = static_cast<std::uint16_t>(*r);
            }

            {
                auto hi = ops.emit_hdmi_int_probe();
                if (!hi)
                    ++pre_.errors;
                pre_.hdmi_int = static_cast<std::uint16_t>(hi ? *hi : 0u);
                auto gm = ops.emit_gamma_probe();
                if (!gm)
                    ++pre_.errors;
                pre_.gamma_cap = static_cast<std::uint16_t>(gm ? *gm : 0u);
            }

            {
                const ArCustomWords arc = unpack_ar_custom(ar_cust_bits_);
                auto a = svc::emit_ar_custom(link.frame(), arc);
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

            const svc::ScalerSlotMode mode = flt_have_ ? flt_local_.filter_mode()
                                                       : policy().filter_mode;
            auto f = ops.emit_filter_mode(mode);
            if (!f) {

                ++pre_.errors;
                pre_step_ = PreludeStep::Burst;
                break;
            }
            pre_.flt_flags = *f;
            const svc::FltFlags flt{*f};

            if (flt.present())
                geo_gate_.last_flt_flags = flt;
            if (!flt.present()) {
                ++pre_.coeff_skips;
                pre_step_ = PreludeStep::Burst;
                break;
            }
            if (flt_have_) {
                svc::VideoSample s{};
                s.flt_flags = flt;
                if (!fs_.arm(
                        stream_ports_(),
                        flt,
                        select_slot(s),
                        FilterStream::Counting::Prelude,
                        std::nullopt
                    )) {

                    if (auto e = svc::emit_filter_coeff_chunk(link.frame(), {}); !e)
                        ++pre_.errors;
                    ++pre_.chunks;
                    pre_step_ = PreludeStep::Burst;
                    break;
                }
            } else {

                fs_.arm_generated(flt, FilterStream::Counting::Prelude);
            }
            pre_step_ = PreludeStep::FltCoef;
            break;
        }

        case PreludeStep::FltCoef: {

            fs_.step(stream_ports_(), link);
            if (!fs_.active())
                pre_step_ = PreludeStep::Burst;
            break;
        }

        case PreludeStep::Burst: {

            if (!parked_) {
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
            pre_step_ = PreludeStep::ShadowMask;
            replay_parked_(link, fence, false);
            break;
        }

        case PreludeStep::ShadowMask: {

            auto r = ops.emit_shadow_mask(static_cast<std::uint16_t>(shadow_mask_word_.v));
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
            if (auto p = ops.emit_afilter_probe(); !p) {
                ++pre_.errors;
                tf |= kTailFailProbe;
            } else {
                af = *p;
            }
            pre_.af_flags = af;

            audio_.accept_probe_reply(static_cast<std::uint8_t>(af), policy().front_end);
            if (auto f = audio_.flush(); !f) {
                ++pre_.errors;
                tf |= kTailFailFlush;
            }
            const std::uint8_t push_vol = audio_.core_volume_byte();
            const std::uint8_t audvol = audio_.audvol_wire_byte();

            if (af != 0) {
                if (auto r = svc::emit_afilter_volume(link.frame(), push_vol); !r) {
                    ++pre_.errors;
                    tf |= kTailFailPush;
                }
            } else {
                ++pre_.afilter_skips;
            }

            if (auto r = svc::emit_audvol(link.frame(), audvol); !r) {
                ++pre_.errors;
                tf |= kTailFailVol;
            }
            if (auto r = emit_but_sw_(link, tail_but_sw_, fence); !r) {
                ++pre_.errors;
                tf |= kTailFailButSw;
            }
            pre_.tail_fail = tf;

            pre_step_ = PreludeStep::Done;

            ++pre_.completions;

            if (parked_)
                replay_parked_(link, fence, true);
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

svc::VFilter VideoWire::select_slot(const svc::VideoSample& s) const {
    const auto enabled = flt_local_.enabled();
    return svc::video_timing::select_vfilter(s, enabled);
}

bool VideoWire::service_filter_publish(proto::Link& link) {
    svc::ProbeOps ops{link};
    const std::uint32_t g = flt_cell_.generation();
    if (g == 0u || g == flt_seen_gen_)
        return false;
    if (!consume_filters())
        return false;

    auto f = ops.emit_filter_mode(flt_local_.filter_mode());
    if (!f) {
        flt_stream_errors_.add();
        return true;
    }
    const svc::FltFlags flt{*f};
    if (!flt.present())
        return true;
    geo_gate_.last_flt_flags = flt;
    svc::VideoSample s = geo_gate_.last;
    s.flt_flags = flt;
    if (!fs_.arm(
            stream_ports_(),
            flt,
            select_slot(s),
            FilterStream::Counting::Stream,
            std::nullopt
        )) {

        if (auto e = svc::emit_filter_coeff_chunk(link.frame(), {}); !e)
            flt_stream_errors_.add();
    }
    return true;
}

bool FilterStream::arm(
    const Ports& p,
    svc::FltFlags flt_flags,
    svc::VFilter slot,
    Counting where_counted,
    std::optional<svc::ScalingWords> tail
) noexcept {
    const svc::FilterFormat fmt = flt_flags.format();
    const svc::FilterBank& h = p.set.bank(svc::VFilter::Horz);
    const svc::FilterBank& b = p.set.bank(slot);
    const bool send_h = !svc::same_digest(h, p.sent_horiz);
    const bool send_v = !svc::same_digest(b, p.sent_vert);
    const svc::FilterSendPlan planned = svc::plan_filter_banks(h, b, fmt, send_h, send_v);
    if (planned.count == 0) {

        p.sent_horiz = h;
        p.sent_vert = b;
        return false;
    }
    plan = planned;
    format = fmt;
    vert_slot = slot;
    seg_i = 0;
    cursor = 0;
    generated = false;
    counting = where_counted;
    tail_words = tail;
    armed = true;
    p.streams.add();
    return true;
}

void FilterStream::arm_generated(svc::FltFlags flt_flags, Counting where_counted) noexcept {
    generated = true;
    counting = where_counted;
    format = flt_flags.format();
    cursor = 0;
    tail_words.reset();
    armed = true;
}

bool VideoWire::plan_filter_send(const svc::VideoSample& s, bool resend) {

    if (!flt_have_)
        return false;

    std::optional<svc::ScalingWords> tail;
    if (resend)
        tail = svc::scaling_words(s, geo_gate_.policy);
    return fs_
        .arm(stream_ports_(), s.flt_flags, select_slot(s), FilterStream::Counting::Stream, tail);
}

void FilterStream::step(const Ports& p, proto::Link& link) noexcept {
    constexpr std::size_t kChunkWords = VideoWire::kCoeffChunkWords;
    std::uint16_t buf[kChunkWords];
    std::size_t n = 0;
    const std::size_t total = generated ? svc::filter_coeff_word_count(format)
                                        : svc::filter_bank_word_count(format);
    if (generated) {
        const std::size_t remaining = total - cursor;
        n = remaining < kChunkWords ? remaining : kChunkWords;
        for (std::size_t i = 0; i < n; ++i)
            buf[i] = svc::filter_coeff_word(format, svc::FilterBankWordIndex{cursor + i});
    } else {
        const svc::FilterSendPlan::Seg& seg = plan.segs[seg_i];
        const svc::FilterBank& b = svc::is_vert(seg.bank) ? p.set.bank(vert_slot)
                                                          : p.set.bank(svc::VFilter::Horz);
        const auto& ph = svc::is_adaptive(seg.bank) ? b.adaptive : b.phases;
        const std::size_t remaining = total - cursor;
        n = remaining < kChunkWords ? remaining : kChunkWords;
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = svc::filter_bank_word(
                std::span<const svc::FilterPhase>(ph),
                format,
                seg.bank,
                svc::FilterBankWordIndex{cursor + i}
            );
        }
    }

    if (auto r = svc::emit_filter_coeff_chunk(link.frame(), std::span<const std::uint16_t>(buf, n));
        !r) {

        p.errors.add();
        if (counting == Counting::Prelude)
            ++p.pre.errors;
        armed = false;
        tail_words.reset();
        return;
    }
    if (counting == Counting::Prelude) {
        ++p.pre.chunks;
        p.pre.coeff_words += static_cast<std::uint32_t>(n);
    } else {
        p.words.add(static_cast<std::uint32_t>(n));
    }

    cursor += n;
    bool done = false;
    if (cursor >= total) {
        if (generated) {
            done = true;
        } else {
            cursor = 0;
            ++seg_i;
            done = seg_i >= plan.count;
        }
    }
    if (!done)
        return;

    armed = false;

    if (generated) {
        p.sent_horiz = svc::nearest_neighbour_bank();
        p.sent_vert = p.sent_horiz;
        generated = false;
    } else {
        p.sent_horiz = p.set.bank(svc::VFilter::Horz);
        p.sent_vert = p.set.bank(vert_slot);
    }
    if (tail_words) {
        const svc::ScalingWords tw = *tail_words;
        tail_words.reset();

        {
            if (auto r = link.command(proto::UioOp::SetHeight); !r) {
                p.errors.add();
                return;
            }
            if (auto r = link.word(tw.height); !r) {
                p.errors.add();
                return;
            }
        }
        {
            if (auto r = link.command(proto::UioOp::SetWidth); !r) {
                p.errors.add();
                return;
            }
            if (auto r = link.word(tw.width); !r) {
                p.errors.add();
                return;
            }
        }
        link.close();
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
    pre_.step = pre_step_;
    prelude_cell_.publish(pre_);
}

PreludeStats VideoWire::prelude_stats() const noexcept {
    return prelude_cell_.sample().value;
}

}
