// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_wire.h"
#include "infra/span_cut.h"

#include "app/link_op_dispatch.h"

#include <cstring>

#include "app/staged_frame_dispatch.h"
#include "hal/spi_sample.h"
#include "infra/prelude.h"
#include "svc/audio_service.h"
#include "svc/but_sw_wire.h"
#include "svc/video/probe_ops.h"

namespace mister::app {

namespace {

constexpr FrameWordCount kModeWordLimit{VideoWire::kWords};
constexpr FrameWordCount kChunkWordLimit{kCoeffChunkWords};

constexpr proto::StageSlot kTableSlots{VideoWire::kSlots};

}

Ex<void> VideoWire::stage(const StagedFrame& f, proto::BindGeneration core) {

    if (const auto mode = infra::as<StagedFrame::Mode>(f)) {
        if (mode->m.count.is_empty() || mode->m.count.exceeds(kModeWordLimit)) {
            staged_rejected_.add();
            return std::unexpected(mode->m.count.refusal(Errc::slot_range, ERR_SITE()));
        }
    }
    if (const auto chunk = infra::as<StagedFrame::CoeffChunk>(f)) {
        if (chunk->count.exceeds(kChunkWordLimit)) {
            staged_rejected_.add();
            return std::unexpected(chunk->count.refusal(Errc::slot_range, ERR_SITE()));
        }
    }

    const proto::StageGen gen = gen_.next();
    const proto::StageSlot slot = gen.slot_in(kTableSlots);

    table_[slot.index()].write(gen.stamp(), f);

    const proto::LinkOp::StagedFrame op{.slot = slot, .bind_gen = core, .gen = gen};
    if (!link_tx_.push(op)) {

        staged_rejected_.add();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), gen.stamp()});
    }
    gen_ = gen;
    staged_.add();
    return {};
}

std::optional<BoundCore> VideoWire::bound_core() const noexcept {

    const auto s = bound_cell_.sample();
    if (!s)
        return std::nullopt;
    return s.value;
}

void VideoWire::bind_core(
    CoreEdgeSeq seq,
    proto::BindGeneration gen,
    os::RawMonotonicClock::time_point now
) noexcept {

    bound_ = gen;
    bound_once_ = true;
    bound_at_ = now;
    settle_bound_fired_ = false;
    bound_cell_.publish(BoundCore{.gen = gen, .core_seq = seq});
}

void VideoWire::take_ui_cells() noexcept {

    PreludeLevel level;
    if (level_seen_.take_if_changed(level_cell_, level))
        level_ = level;
}

bool VideoWire::prelude_settled(os::RawMonotonicClock::time_point now) noexcept {

    if (level_.core == bound_ && level_.settled)
        return true;
    if (bound_once_ && now - bound_at_ >= kSettleBound) {

        if (!settle_bound_fired_) {
            settle_bound_fired_ = true;
            settle_bound_fires_.add();
        }
        return true;
    }
    return false;
}

Delivered VideoWire::on(
    const proto::LinkOp::StagedFrame& op,
    const LinkOpCtx& ctx,
    proto::link::Link& link
) noexcept {

    if (op.bind_gen != bound_) {
        stale_core_.add();
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }
    const proto::StageSlot slot = op.slot;
    if (!slot.within(kTableSlots)) {
        emit_rejected_.add();
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }

    StagedFrame local{};
    if (!op.gen.is_set() || table_[slot.index()].try_read(local) != op.gen.stamp()) {
        stale_.add();
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }

    const std::size_t k = infra::ordinal(local.kind);
    if (k >= kStagedFrameFacts.size() || !admitted(kStagedFrameFacts[k].window, ctx)) {
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }
    return run_frame_(local, op, link, infra::OptRef<proto::IResetFence>{ctx.fence});
}

Delivered VideoWire::run_frame_(
    const StagedFrame& f,
    const proto::LinkOp::StagedFrame& op,
    proto::link::Link& link,
    infra::OptRef<proto::IResetFence> fence
) noexcept {
    FrameArms arms{.link = link, .fence = fence, .replies = replies_, .self = *this, .op = op};
    return infra::dispatch<StagedFrameRoutes>(f, arms);
}

Delivered VideoWire::misrouted(
    const proto::LinkOp&,
    const LinkOpCtx&,
    proto::link::Link&
) noexcept {
    return Delivered{ILinkEncoder::Outcome::Dropped, false};
}

void VideoWire::FrameArms::answer_(
    proto::LinkEvent::ReplyWindow w,
    proto::WindowFails fails,
    proto::ReplyWord word
) noexcept {
    const proto::LinkEvent::FrameReply
        reply{.window = w, .fails = fails, .word = word, .bind_gen = self.bound_, .gen = op.gen};
    if (!replies.push(reply))
        self.reply_drops_.add();
}

Delivered VideoWire::FrameArms::on(const StagedFrame::Mode& m) noexcept {
    const auto r = svc::video::emit_video_words(
        link.frame(),
        m.m.opcode,
        infra::affine::first(std::span<const hal::SpiWord>(m.m.w), m.m.count)
    );

    const bool burst = m.m.opcode == proto::UioOp::SetVideo;
    if (!r) {

        self.partial_.add();
        if (burst)
            answer_(
                proto::LinkEvent::ReplyWindow::ModeEmitted,
                proto::WindowFails{}.with(FrameWindow::ModeEmitted),
                proto::ReplyWord{}
            );
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }
    self.emitted_.add();
    if (burst)
        answer_(
            proto::LinkEvent::ReplyWindow::ModeEmitted,
            proto::WindowFails{},
            proto::ReplyWord::of(proto::word(m.m.opcode))
        );

    if (auto arm = self.emit_but_sw_(link, self.tail_but_sw_, fence); !arm)
        self.arm_fail_.add();
    return Delivered{ILinkEncoder::Outcome::Encoded, false};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::ModeInBlock& m) noexcept {
    const auto r = svc::video::emit_video_words(
        link.frame(),
        m.m.opcode,
        infra::affine::first(std::span<const hal::SpiWord>(m.m.w), m.m.count)
    );

    const bool burst = m.m.opcode == proto::UioOp::SetVideo;
    if (!r) {

        self.partial_.add();
        if (burst)
            answer_(
                proto::LinkEvent::ReplyWindow::ModeEmitted,
                proto::WindowFails{}.with(FrameWindow::ModeEmitted),
                proto::ReplyWord{}
            );
        return Delivered{ILinkEncoder::Outcome::Encoded, true};
    }
    self.emitted_.add();
    if (burst)
        answer_(
            proto::LinkEvent::ReplyWindow::ModeEmitted,
            proto::WindowFails{},
            proto::ReplyWord::of(proto::word(m.m.opcode))
        );
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::FbufProbes& f) noexcept {
    svc::video::ProbeOps ops{link};

    {
        const auto fb = svc::video::emit_fbuf(link.frame(), hal::SpiWord{});
        proto::ReplyWord word{};
        proto::WindowFails fails{};
        if (fb) {
            word = proto::ReplyWord::of(*fb);
        } else {
            fails = fails.with(FbufWindow::Fbuf);
            self.partial_.add();
        }
        answer_(proto::LinkEvent::ReplyWindow::FbufAck, fails, word);
    }

    {
        const auto hi = ops.emit_hdmi_int_probe();
        proto::ReplyWord word{};
        proto::WindowFails fails{};
        if (hi) {
            word = proto::ReplyWord::of(*hi);
        } else {
            fails = fails.with(FbufWindow::HdmiProbe);
            self.partial_.add();
        }
        answer_(proto::LinkEvent::ReplyWindow::HdmiInt, fails, word);
    }
    {
        const auto gm = ops.emit_gamma_probe();
        proto::ReplyWord word{};
        proto::WindowFails fails{};
        if (gm) {
            word = proto::ReplyWord::of(*gm);
        } else {
            fails = fails.with(FbufWindow::GammaProbe);
            self.partial_.add();
        }
        answer_(proto::LinkEvent::ReplyWindow::GammaCap, fails, word);
    }

    if (auto ar = svc::video::emit_ar_custom(link.frame(), f.ar_custom); !ar)
        self.partial_.add();
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::FilterMode& m) noexcept {
    svc::video::ProbeOps ops{link};
    const auto f = ops.emit_filter_mode(m.mode);
    if (!f) {
        self.partial_.add();
        answer_(
            proto::LinkEvent::ReplyWindow::FltNum,
            proto::WindowFails{}.with(FrameWindow::FltNum),
            proto::ReplyWord{}
        );
        return Delivered{ILinkEncoder::Outcome::Encoded, true};
    }
    const svc::FltFlags flt = svc::FltFlags::of(*f);

    if (flt.present())
        self.geo_gate_.last_flt_flags = flt;
    answer_(proto::LinkEvent::ReplyWindow::FltNum, proto::WindowFails{}, proto::ReplyWord::of(*f));
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::CoeffChunk& c) noexcept {
    if (auto r = svc::video::emit_filter_coeff_chunk(
            link.frame(),
            infra::affine::first(std::span<const hal::SpiWord>(c.w), c.count)
        );
        !r)
        self.partial_.add();
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::ShadowMask& s) noexcept {
    svc::video::ProbeOps ops{link};
    const auto r = ops.emit_shadow_mask(s.flag);
    if (!r) {
        self.partial_.add();
        answer_(
            proto::LinkEvent::ReplyWindow::ShadowCap,
            proto::WindowFails{}.with(FrameWindow::ShadowCap),
            proto::ReplyWord{}
        );
        return Delivered{ILinkEncoder::Outcome::Encoded, true};
    }
    answer_(
        proto::LinkEvent::ReplyWindow::ShadowCap,
        proto::WindowFails{},
        proto::ReplyWord::of(*r)
    );
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::AudioTail& t) noexcept {
    svc::video::ProbeOps ops{link};
    proto::WindowFails fails{};
    proto::ReplyWord r{};
    if (const auto p = ops.emit_afilter_probe(); p) {
        r = proto::ReplyWord::of(*p);
    } else {
        fails = fails.with(TailWindow::Probe);
    }

    if (r.nonzero()) {
        const svc::CoreVolume lead = t.lead.pick(r);
        Ex<void> pushed{};
        switch (t.push) {
            case AfilterPushForm::Withheld:
                break;
            case AfilterPushForm::Byte:
                pushed = svc::emit_afilter_volume(link.frame(), lead);
                break;
            case AfilterPushForm::Stream:
                pushed = svc::emit_afilter_stream(
                    link.frame(),
                    lead,
                    infra::affine::first(std::span<const hal::SpiWord>(t.words), t.count)
                );
                break;
        }
        if (!pushed)
            fails = fails.with(TailWindow::Push);
    }

    if (auto e = svc::emit_audvol(link.frame(), t.audvol.pick(r)); !e)
        fails = fails.with(TailWindow::AudVol);

    if (auto e = self.emit_but_sw_(link, self.tail_but_sw_, fence); !e)
        fails = fails.with(TailWindow::ButSw);

    answer_(proto::LinkEvent::ReplyWindow::AfilterProbe, fails, r);
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::AudVol& a) noexcept {

    if (auto e = svc::emit_audvol(link.frame(), a.vol); !e)
        self.partial_.add();
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::on(const StagedFrame::AfilterPush& a) noexcept {

    svc::video::ProbeOps ops{link};
    proto::WindowFails fails{};
    proto::ReplyWord r{};
    if (const auto p = ops.emit_afilter_probe(); p) {
        r = proto::ReplyWord::of(*p);
    } else {
        fails = fails.with(TailWindow::Probe);
    }

    if (r.nonzero()) {
        const svc::CoreVolume lead = a.lead.pick(r);
        Ex<void> pushed{};
        switch (a.push) {
            case AfilterPushForm::Withheld:
                break;
            case AfilterPushForm::Byte:
                pushed = svc::emit_afilter_volume(link.frame(), lead);
                break;
            case AfilterPushForm::Stream:
                pushed = svc::emit_afilter_stream(
                    link.frame(),
                    lead,
                    infra::affine::first(std::span<const hal::SpiWord>(a.words), a.count)
                );
                break;
        }
        if (!pushed)
            fails = fails.with(TailWindow::Push);
    }

    answer_(proto::LinkEvent::ReplyWindow::AfilterProbe, fails, r);
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

Delivered VideoWire::FrameArms::misrouted(const StagedFrame&) noexcept {
    return Delivered{ILinkEncoder::Outcome::Dropped, false};
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
    s.stale_core = stale_core_.get();
    s.reply_drops = reply_drops_.get();
    s.settle_bound_fires = settle_bound_fires_.get();
    return s;
}

void VideoWire::on_rt_round(
    proto::link::Link& link,
    std::int64_t now_ns,
    infra::OptRef<proto::IResetFence> fence
) {

    const os::RawMonotonicClock::time_point now{std::chrono::nanoseconds{now_ns}};
    (void)fence;
    svc::video::ProbeOps ops{link};
    geo_rounds_.add();

    take_ui_cells();

    if (auto v = policy_seen_.take_if_changed(policy_cell_))
        policy_ = *v;

    if (!prelude_settled(now))
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

    geo_cell_.publish(
        VideoGeoLatch{
            .sample = pass->sample,
            .core_seq = geo_core_seq_,
            .filter_seq = filter_seq_,
            .scaling_seq = scaling_seq_
        }
    );
}

void VideoWire::reset_geometry(CoreEdgeSeq core_seq) noexcept {
    geo_core_seq_ = core_seq;
    geo_gate_ = svc::video::GeometryGate{};

    geo_force_next_ = true;

    geo_next_.reset();

    geo_cell_.invalidate();
}

Ex<svc::video::VideoSample> VideoWire::sample() {
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

bool VideoWire::update_but_sw(
    proto::link::Link& link,
    hal::ButtonMask buttons,
    bool osd_visible,
    infra::OptRef<proto::IResetFence> fence
) noexcept {
    const proto::ConfSwitches composed = tail_but_sw_.with_buttons(buttons);
    if (but_sw_seen_ && composed == but_sw_shadow_)
        return false;

    proto::ConfSwitches wire = composed;
    if (osd_visible)
        wire = wire.without(proto::ConfSwitches::Bit::UserButton);
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
    proto::link::Link& link,
    proto::ConfSwitches word,
    infra::OptRef<proto::IResetFence> fence
) noexcept {
    if (fence)
        fence->before_buttons(but_sw_sent_, word);
    auto r = svc::emit_but_sw(link.frame(), word);
    if (r)
        but_sw_sent_ = word;
    return r;
}

bool VideoWire::plan_filter_send(const svc::video::VideoSample&, bool resend) {

    filter_seq_ = filter_seq_.next();
    if (resend)
        scaling_seq_ = scaling_seq_.next();
    return true;
}

std::optional<VideoGeoLatch> VideoWire::geo_latch() const noexcept {
    const auto s = geo_cell_.sample();
    if (!s)
        return std::nullopt;
    return s.value;
}

svc::video::ScalingPolicy VideoWire::published_policy() const noexcept {

    return policy_cell_.sample().value;
}

Delivered VideoWire::FrameArms::on(const StagedFrame::ScalingPair& p) noexcept {

    if (auto r = link.command(proto::UioOp::SetHeight); !r) {
        self.partial_.add();
        return Delivered{ILinkEncoder::Outcome::Encoded, true};
    }
    if (auto r = link.word(p.words.height); !r) {
        self.partial_.add();
        return Delivered{ILinkEncoder::Outcome::Encoded, true};
    }
    if (auto r = link.command(proto::UioOp::SetWidth); !r) {
        self.partial_.add();
        return Delivered{ILinkEncoder::Outcome::Encoded, true};
    }
    if (auto r = link.word(p.words.width); !r) {
        self.partial_.add();
        return Delivered{ILinkEncoder::Outcome::Encoded, true};
    }
    link.close();
    return Delivered{ILinkEncoder::Outcome::Encoded, true};
}

}
