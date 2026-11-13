// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/prelude_composer.h"

#include <algorithm>
#include <cstring>

#include "svc/scaling_words.h"

namespace mister::app {

namespace {

constexpr proto::WindowFails kFailsNone{};

}

void PreludeComposer::arm(bool front_end) noexcept {
    step_ = PreludeStep::Owed;
    bound_ok_ = false;
    began_ = false;
    fbuf_owed_ = false;
    filter_owed_ = false;
    front_end_ = front_end;
    set_.reset();
    held_ = {};
    flight_ = {};
    release_ = Release::None;
    mode_in_block_done_ = shadow_done_ = tail_done_ = false;
    deadline_.reset();
    bare_ = false;
    bare_no_digests_ = false;
    wheel_active_ = false;
    pair_owed_ = false;
    answers_ = Answers{};
    ++stats_.arms;
    if (const auto b = w_.wire.bound_core()) {
        bound_ = b->gen;
        bound_ok_ = true;
        w_.wire.publish_prelude_level(PreludeLevel{.core = bound_, .settled = false});
    }
    publish_stats_();
}

void PreludeComposer::begin(const Inputs& in) noexcept {
    if (step_ != PreludeStep::Owed)
        return;

    w_.audio.load_for_core(in.core, front_end_, in.afilter_default);
    inputs_.ar_custom = in.ar_custom;
    inputs_.shadow = in.shadow;
    fbuf_owed_ = true;
}

void PreludeComposer::take_filters(std::unique_ptr<svc::FilterSet> set) noexcept {
    if (step_ == PreludeStep::Idle || step_ == PreludeStep::Done)
        return;
    set_ = std::move(set);
    filter_owed_ = set_ != nullptr;
}

Ex<void> PreludeComposer::publish(proto::UioOp op, std::span<const std::uint16_t> w) {
    TASTY_SEAT_BODY(PreludeComposer);
    const Publish words{w};
    if (!words.fits_block()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), words.detail()});
    }

    if (step_ == PreludeStep::Idle || step_ == PreludeStep::Done) {
        if (!w_.wire.may_stage()) {
            hold_(op, words);
            publish_stats_();
            return {};
        }
        StagedFrame::Mode m{};
        m.m.opcode = op;
        m.m.count = words.count();
        const std::span<const std::uint16_t> from = words.words();
        std::memcpy(m.m.w.data(), from.data(), from.size() * sizeof(std::uint16_t));
        return stage_(infra::make<StagedFrame>(m), StagedFrame::Kind::Mode, RepliesOwed{});
    }

    hold_(op, words);
    publish_stats_();
    return {};
}

void PreludeComposer::on_reply(const proto::LinkEvent::FrameReply& r) noexcept {
    InFlight* f = find_flight_(r.gen);
    if (f == nullptr)
        return;
    if (r.fails != kFailsNone)
        ++stats_.errors;
    const StagedFrame::Kind kind = f->kind;
    if (f->remaining.is_set())
        f->remaining.settle_one();

    switch (kind) {
        case StagedFrame::Kind::FbufProbes:
            switch (r.window) {
                case proto::LinkEvent::ReplyWindow::FbufAck:
                    stats_.fb_ack = r.word;
                    break;
                case proto::LinkEvent::ReplyWindow::HdmiInt:
                    stats_.hdmi_int = r.word;
                    answers_.hdmi_int = r.word;
                    break;
                case proto::LinkEvent::ReplyWindow::GammaCap:
                    stats_.gamma_cap = r.word;
                    break;
                case proto::LinkEvent::ReplyWindow::FltNum:
                case proto::LinkEvent::ReplyWindow::ShadowCap:
                case proto::LinkEvent::ReplyWindow::AfilterProbe:
                case proto::LinkEvent::ReplyWindow::kCount:
                    break;
            }
            break;
        case StagedFrame::Kind::FilterMode:
            if (r.window == proto::LinkEvent::ReplyWindow::FltNum)
                on_fltnum_(r);
            break;
        case StagedFrame::Kind::ShadowMask:
            stats_.sm_cap = r.word;
            break;
        case StagedFrame::Kind::AudioTail:
            if (r.window == proto::LinkEvent::ReplyWindow::AfilterProbe) {
                stats_.af_flags = r.word;
                stats_.tail_fail = r.fails;
                if (!r.word.nonzero())
                    ++stats_.afilter_skips;
                w_.audio.on_probe_reply(r.word, front_end_);
                finish_block_();
            }
            break;
        case StagedFrame::Kind::Mode:
        case StagedFrame::Kind::ModeInBlock:
        case StagedFrame::Kind::CoeffChunk:
        case StagedFrame::Kind::ScalingPair:
        case StagedFrame::Kind::AudVol:
        case StagedFrame::Kind::AfilterPush:
        case StagedFrame::Kind::kCount:
            break;
    }
    publish_stats_();
}

void PreludeComposer::on_fltnum_(const proto::LinkEvent::FrameReply& r) noexcept {
    if (set_ == nullptr)
        return;
    if (r.fails == kFailsNone && r.word.nonzero()) {
        stats_.flt_flags = r.word;
        const svc::FltFlags flt = FilterReply::of(r.word).flags();

        svc::VideoSample s{};
        s.flt_flags = flt;
        vert_ = svc::video_timing::select_vfilter(s, set_->enabled());
        fmt_ = flt.format();
        const svc::FilterBank& h = set_->bank(svc::VFilter::Horz);
        const svc::FilterBank& b = set_->bank(vert_);
        plan_ = svc::plan_filter_banks(
            h,
            b,
            fmt_,
            !svc::same_digest(h, sent_h_),
            !svc::same_digest(b, sent_v_)
        );
        if (plan_.count == 0) {

            bare_ = true;
            step_ = PreludeStep::Streaming;
            ++stats_.steps;
        } else {
            seg_ = {};
            cursor_ = {};
            step_ = PreludeStep::Streaming;
            ++stats_.steps;
        }
        return;
    }

    ++stats_.coeff_skips;
    sent_h_ = svc::FilterBank{};
    sent_v_ = svc::FilterBank{};
    begin_release_();
}

void PreludeComposer::service_stream_() noexcept {
    if (step_ != PreludeStep::Streaming || set_ == nullptr)
        return;

    if (release_ != Release::None)
        return;
    if (bare_) {
        if (!w_.wire.may_stage())
            return;
        StagedFrame::CoeffChunk c{};
        if (!stage_(infra::make<StagedFrame>(c), StagedFrame::Kind::CoeffChunk, RepliesOwed{}))
            return;
        ++stats_.chunks;
        bare_ = false;
        if (!bare_no_digests_) {
            sent_h_ = set_->bank(svc::VFilter::Horz);
            sent_v_ = set_->bank(vert_);
        }
        bare_no_digests_ = false;
        begin_release_();
        return;
    }
    const std::size_t total = svc::filter_bank_word_count(fmt_);
    while (w_.wire.may_stage()) {
        const svc::FilterSendPlan::Seg& seg = plan_.seg(seg_);
        const svc::FilterBank& b = svc::is_vert(seg.bank) ? set_->bank(vert_)
                                                          : set_->bank(svc::VFilter::Horz);
        const auto& ph = svc::is_adaptive(seg.bank) ? b.adaptive : b.phases;

        const std::size_t at = cursor_.wire_offset(svc::FilterAddressGrid{});
        const ChunkWords words{std::min(total - at, kCoeffChunkWords)};
        StagedFrame::CoeffChunk c{};
        c.count = words.frame_words();
        words.fill(c.w, cursor_, [&](svc::FilterBankWordIndex word) {
            return svc::filter_bank_word(
                std::span<const svc::FilterPhase>(ph),
                fmt_,
                seg.bank,
                word
            );
        });
        if (!stage_(infra::make<StagedFrame>(c), StagedFrame::Kind::CoeffChunk, RepliesOwed{}))
            return;
        ++stats_.chunks;
        stats_.coeff_words += words.stats_word();
        ++stats_.steps;
        cursor_ = words.past(cursor_);
        if (cursor_.wire_offset(svc::FilterAddressGrid{}) < total)
            continue;
        cursor_ = svc::FilterBankWordIndex{};
        seg_ = seg_.next();
        if (plan_.covers(seg_))
            continue;

        sent_h_ = set_->bank(svc::VFilter::Horz);
        sent_v_ = set_->bank(vert_);
        begin_release_();
        return;
    }
}

void PreludeComposer::service_wheel_() noexcept {

    if (step_ != PreludeStep::Idle || !bound_ok_)
        return;
    const auto core = w_.wire.bound_core();
    const auto latch = w_.wire.geo_latch();

    if (!core || !latch || CoreEdgeSeq{latch->core_seq} != core->core_seq)
        return;

    if (!wheel_active_ && latch->filter_seq != last_filter_seq_) {

        last_filter_seq_ = latch->filter_seq;
        wheel_active_ = true;
        bare_ = false;
        seg_ = {};
        cursor_ = {};
        if (set_ == nullptr) {

            fmt_ = svc::FltFlags{}.format();
            vert_ = svc::VFilter::Horz;
            bare_ = true;
        } else {
            fmt_ = latch->sample.flt_flags.format();
            vert_ = svc::video_timing::select_vfilter(latch->sample, set_->enabled());
            const svc::FilterBank& h = set_->bank(svc::VFilter::Horz);
            const svc::FilterBank& b = set_->bank(vert_);
            plan_ = svc::plan_filter_banks(
                h,
                b,
                fmt_,
                !svc::same_digest(h, sent_h_),
                !svc::same_digest(b, sent_v_)
            );
            bare_ = !plan_.covers(seg_);
        }
    }

    while (wheel_active_ && w_.wire.may_stage()) {
        if (bare_) {
            StagedFrame::CoeffChunk c{};
            if (!stage_(infra::make<StagedFrame>(c), StagedFrame::Kind::CoeffChunk, RepliesOwed{}))
                break;
            ++stats_.chunks;
            bare_ = false;
            wheel_active_ = false;
            break;
        }
        const std::size_t total = svc::filter_bank_word_count(fmt_);
        const svc::FilterSendPlan::Seg& seg = plan_.seg(seg_);
        const svc::FilterBank& b = svc::is_vert(seg.bank) ? set_->bank(vert_)
                                                          : set_->bank(svc::VFilter::Horz);
        const auto& ph = svc::is_adaptive(seg.bank) ? b.adaptive : b.phases;
        const std::size_t at = cursor_.wire_offset(svc::FilterAddressGrid{});
        const ChunkWords words{std::min(total - at, kCoeffChunkWords)};
        StagedFrame::CoeffChunk c{};
        c.count = words.frame_words();
        words.fill(c.w, cursor_, [&](svc::FilterBankWordIndex word) {
            return svc::filter_bank_word(
                std::span<const svc::FilterPhase>(ph),
                fmt_,
                seg.bank,
                word
            );
        });
        if (!stage_(infra::make<StagedFrame>(c), StagedFrame::Kind::CoeffChunk, RepliesOwed{}))
            break;
        ++stats_.chunks;
        stats_.coeff_words += words.stats_word();
        cursor_ = words.past(cursor_);
        if (cursor_.wire_offset(svc::FilterAddressGrid{}) < total)
            continue;
        cursor_ = svc::FilterBankWordIndex{};
        seg_ = seg_.next();
        if (plan_.covers(seg_))
            continue;

        sent_h_ = set_->bank(svc::VFilter::Horz);
        sent_v_ = set_->bank(vert_);
        wheel_active_ = false;
    }

    if (wheel_active_)
        return;
    if (const auto now_latch = w_.wire.geo_latch();
        now_latch && now_latch->scaling_seq != last_scaling_seq_) {
        pair_owed_ = true;
    }
    if (pair_owed_) {
        if (!w_.wire.may_stage())
            return;
        const auto at = w_.wire.geo_latch();
        if (!at)
            return;
        const svc::ScalingWords words = svc::scaling_words(at->sample, w_.wire.published_policy());
        const StagedFrame::ScalingPair p{.words = words};
        if (!stage_(infra::make<StagedFrame>(p), StagedFrame::Kind::ScalingPair, RepliesOwed{}))
            return;
        pair_owed_ = false;
        last_scaling_seq_ = at->scaling_seq;
    }
}

void PreludeComposer::service() noexcept {

    if (step_ == PreludeStep::Owed && !bound_ok_) {
        if (const auto b = w_.wire.bound_core()) {
            bound_ = b->gen;
            bound_ok_ = true;
            w_.wire.publish_prelude_level(PreludeLevel{.core = bound_, .settled = false});
        }
    }

    if (step_ == PreludeStep::Owed && bound_ok_ && !began_ && fbuf_owed_ && w_.wire.may_stage()) {
        const StagedFrame::FbufProbes p{.ar_custom = inputs_.ar_custom};
        if (stage_(
                infra::make<StagedFrame>(p),
                StagedFrame::Kind::FbufProbes,
                kFbufProbesReplies
            )) {
            fbuf_owed_ = false;
            began_ = true;
            ++stats_.steps;
        }
    }
    if (step_ == PreludeStep::Owed
        && bound_ok_
        && began_
        && filter_owed_
        && set_ != nullptr
        && w_.wire.may_stage()) {
        const StagedFrame::FilterMode m{.mode = set_->filter_mode()};
        if (stage_(infra::make<StagedFrame>(m), StagedFrame::Kind::FilterMode, RepliesOwed{1})) {
            filter_owed_ = false;
            step_ = PreludeStep::AwaitFltNum;
            deadline_ = now() + kReplyDeadline;
            ++stats_.steps;
        }
    }

    if (step_ == PreludeStep::AwaitFltNum && deadline_ && now() >= *deadline_) {

        ++stats_.barrier_timeouts;
        deadline_.reset();
        if (set_ != nullptr) {
            fmt_ = svc::FltFlags{}.format();
            vert_ = svc::VFilter::Horz;
            bare_ = true;
            bare_no_digests_ = true;
            step_ = PreludeStep::Streaming;
            ++stats_.steps;
        } else {
            begin_release_();
        }
    }

    service_stream_();

    if (release_ != Release::None)
        release_step_();

    service_wheel_();

    if (step_ == PreludeStep::AwaitTail && deadline_ && now() >= *deadline_) {

        ++stats_.barrier_timeouts;
        deadline_.reset();
        if (release_ == Release::AudioTail)
            return;
        if (step_ != PreludeStep::Done)
            finish_block_();
    }

    publish_stats_();
}

void PreludeComposer::begin_release_() noexcept {
    release_ = Release::ModeInBlock;
    mode_in_block_done_ = shadow_done_ = tail_done_ = false;
    release_step_();
}

void PreludeComposer::release_step_() noexcept {
    if (release_ == Release::ModeInBlock) {
        if (!mode_in_block_done_) {
            if (Held* h = find_held_(proto::UioOp::SetVideo)) {
                if (!w_.wire.may_stage())
                    return;
                StagedFrame::ModeInBlock m{};
                m.m.opcode = h->op;
                m.m.count = h->n;
                const std::span<const std::uint16_t> words = h->words();
                std::memcpy(m.m.w.data(), words.data(), words.size() * sizeof(std::uint16_t));
                if (!stage_(
                        infra::make<StagedFrame>(m),
                        StagedFrame::Kind::ModeInBlock,
                        RepliesOwed{}
                    ))
                    return;
                h->live = false;
                h->n = FrameWordCount{};
            }
            mode_in_block_done_ = true;
        }
        release_ = Release::ShadowMask;
    }
    if (release_ == Release::ShadowMask) {
        if (!shadow_done_) {
            if (!w_.wire.may_stage())
                return;
            const StagedFrame::ShadowMask s{.flag = inputs_.shadow};
            if (!stage_(infra::make<StagedFrame>(s), StagedFrame::Kind::ShadowMask, RepliesOwed{1}))
                return;
            shadow_done_ = true;
        }
        release_ = Release::AudioTail;
    }
    if (release_ == Release::AudioTail) {
        if (!tail_done_) {
            if (!w_.wire.may_stage())
                return;
            const StagedFrame::AudioTail t = w_.audio.tail(front_end_);
            if (!stage_(infra::make<StagedFrame>(t), StagedFrame::Kind::AudioTail, RepliesOwed{1}))
                return;
            tail_done_ = true;
            step_ = PreludeStep::AwaitTail;
            deadline_ = now() + kReplyDeadline;
            ++stats_.steps;
        }
        release_ = Release::None;
    }
    if (release_ == Release::Modes) {

        for (Held& h : held_) {
            if (!h.live)
                continue;
            if (!w_.wire.may_stage())
                return;
            StagedFrame::Mode m{};
            m.m.opcode = h.op;
            m.m.count = h.n;
            const std::span<const std::uint16_t> words = h.words();
            std::memcpy(m.m.w.data(), words.data(), words.size() * sizeof(std::uint16_t));
            if (!stage_(infra::make<StagedFrame>(m), StagedFrame::Kind::Mode, RepliesOwed{}))
                return;
            h.live = false;
            h.n = FrameWordCount{};
        }
        w_.wire.publish_prelude_level(PreludeLevel{.core = bound_, .settled = true});
        step_ = PreludeStep::Idle;
        release_ = Release::None;
        ++stats_.steps;
    }
}

void PreludeComposer::finish_block_() noexcept {
    if (step_ == PreludeStep::Done)
        return;
    step_ = PreludeStep::Done;
    ++stats_.completions;

    answers_.completions = PreludeCompletions{stats_.completions};
    deadline_.reset();
    release_ = Release::Modes;
    release_step_();
}

void PreludeComposer::abandon() noexcept {
    step_ = PreludeStep::Idle;
    bound_ok_ = false;
    began_ = false;
    fbuf_owed_ = false;
    filter_owed_ = false;
    set_.reset();
    held_ = {};
    flight_ = {};
    release_ = Release::None;
    deadline_.reset();
    bare_ = false;
    bare_no_digests_ = false;
    wheel_active_ = false;
    pair_owed_ = false;

    bound_ = proto::BindGeneration{};
    publish_stats_();
}

bool PreludeComposer::holding_mode() const noexcept {
    return find_held_(proto::UioOp::SetVideo) != nullptr;
}

PreludeComposer::HeldCount PreludeComposer::held_count_() const noexcept {
    HeldCount n{};
    for (const Held& h : held_)
        if (h.live)
            n.add_one();
    return n;
}

PreludeComposer::Held* PreludeComposer::find_held_(proto::UioOp op) noexcept {
    for (Held& h : held_)
        if (h.live && h.op == op)
            return &h;
    return nullptr;
}

const PreludeComposer::Held* PreludeComposer::find_held_(proto::UioOp op) const noexcept {
    for (const Held& h : held_)
        if (h.live && h.op == op)
            return &h;
    return nullptr;
}

void PreludeComposer::hold_(proto::UioOp op, Publish words) noexcept {
    const std::span<const std::uint16_t> from = words.words();
    if (Held* h = find_held_(op)) {
        h->n = words.count();
        std::memcpy(h->w.data(), from.data(), from.size() * sizeof(std::uint16_t));
        return;
    }
    for (Held& h : held_) {
        if (h.live)
            continue;
        h.live = true;
        h.op = op;
        h.n = words.count();
        std::memcpy(h.w.data(), from.data(), from.size() * sizeof(std::uint16_t));
        ++stats_.parked;
        return;
    }

    ++stats_.errors;
}

PreludeComposer::InFlight* PreludeComposer::find_flight_(proto::StageGen gen) noexcept {
    for (InFlight& f : flight_)
        if (f.remaining.is_set() && f.gen == gen)
            return &f;
    return nullptr;
}

bool PreludeComposer::owns(proto::StageGen gen) const noexcept {
    for (const InFlight& f : flight_)
        if (f.remaining.is_set() && f.gen == gen)
            return true;
    return false;
}

Ex<void> PreludeComposer::stage_(
    const StagedFrame& f,
    StagedFrame::Kind kind,
    RepliesOwed replies
) noexcept {
    if (!w_.wire.may_stage())
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    auto r = w_.wire.stage(f, bound_);
    if (!r)
        return r;
    if (replies.is_set()) {
        for (InFlight& slot : flight_) {
            if (slot.remaining.is_set())
                continue;
            slot.gen = w_.wire.staged_generation();
            slot.kind = kind;
            slot.remaining = replies;
            break;
        }
    }
    return {};
}

void PreludeComposer::publish_stats_() noexcept {
    stats_.step = step_;
    stats_.parked = held_count_().stats_word();
    stats_cell_.publish(stats_);
}

}
