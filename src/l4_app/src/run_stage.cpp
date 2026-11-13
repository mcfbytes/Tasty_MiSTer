// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/run_stage.h"

#include <cstdio>
#include <string_view>

#include "app/core_input_binding.h"
#include "app/osd_close.h"
#include "app/screenshot_pump.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "infra/diag_log.h"
#include "infra/message_sum.h"
#include "svc/types.h"

namespace mister::app {

using Refusal = PlayRefusal::Why;

IReplayStage::Stage RunStage::level() const noexcept {
    TASTY_SEAT_BODY(RunStage);
    return level_;
}

IReplayStage::Step RunStage::enter(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(RunStage);
    return arm_(ctx);
}

IReplayStage::Step RunStage::stop(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(RunStage);
    if (level_ == Stage::Stopping)
        return Step::stay();
    publish_(ctx, ReplayOp::Stop);
    level_ = Stage::Stopping;
    since_ = ctx.w.clock.now();
    return Step::stay();
}

std::optional<ReplayStatus> RunStage::sample_status_(Ctx ctx) const noexcept {
    ReplayStatus s{};
    if (ctx.w.status.sample_into(s) == 0)
        return std::nullopt;
    return s;
}

void RunStage::publish_(Ctx ctx, ReplayOp op) noexcept {
    ctx.w.control.publish(ReplayControl{.gen = ctx.run.gen, .op = op});
}

IReplayStage::Step RunStage::arm_(Ctx ctx) noexcept {
    const ReplayPlay::Request& req = ctx.request;
    const ReplayPlay::ScannedMovie& scan = ctx.scan;
    const ReplayPlay::ArmPlan& arm = ctx.arm;
    ctx.run.gen = ctx.run.gen.next_live();
    next_run_ = 0;
    end_pushed_ = false;
    ctx.run.pushed = FrameCount{};

    publish_(ctx, ReplayOp::Play);
    const cores::IReplayCore::Raster r = req.codec->core().raster(scan.facts);
    const ReplayMsg::Arm a{
        .ports = scan.facts.ports,
        .rom_index = arm.rom_index,
        .vsync_ok = scan.vsync_ok,
        .p0 = arm.p0,
        .lead = arm.lead_field,
        .event = arm.event,
        .late_frames = req.codec->core().power_on(scan.facts).late_frames,
        .offset_us = scan.offset_us,
        .period_ns = std::chrono::duration_cast<CodecNs>(r.period_ns),
        .line0_ns = std::chrono::duration_cast<CodecNs>(r.line0_ns),
        .poweron_ns = arm.poweron_ns
    };
    if (!ctx.w.ring.push(infra::make<ReplayMsg>(a, ReplayMsg::Head{ctx.run.gen}))) {
        publish_(ctx, ReplayOp::Stop);
        return Step::refuse(Refusal::Busy);
    }
    level_ = Stage::Arming;
    since_ = ctx.w.clock.now();
    return Step::stay();
}

void RunStage::stream_(Ctx ctx) noexcept {
    const ReplayPlay::ScannedMovie& scan = ctx.scan;
    while (next_run_ < scan.runs.size()) {
        const ReplayMsg::Input& run = scan.runs[next_run_];
        if (!ctx.w.ring.push(infra::make<ReplayMsg>(run, ReplayMsg::Head{ctx.run.gen})))
            return;
        ctx.run.pushed = run.last - MovieFrame{} + FrameCount{1};
        ++next_run_;
    }
    end_pushed_ = ctx.w.ring.push(
        infra::make<ReplayMsg>(
            ReplayMsg::End{.frame = MovieFrame{} + scan.movie_frames},
            ReplayMsg::Head{ctx.run.gen}
        )
    );
}

IReplayStage::Step RunStage::tick(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(RunStage);
    const auto s = sample_status_(ctx);
    const bool fresh = s && s->gen == ctx.run.gen;
    if (fresh && s->level == ReplayLevel::Idle && s->end != ReplayEnd::None)
        return finish_(ctx, *s);
    if (level_ == Stage::Stopping) {
        if (ctx.w.clock.now() - since_ > ReplayPlay::kAnswerNs)
            return finish_(ctx, ReplayStatus{.gen = ctx.run.gen, .end = ReplayEnd::Stopped});
        return Step::stay();
    }
    if (level_ == Stage::Arming || level_ == Stage::Loading) {
        const Step st = tick_arming_(ctx, s.value_or(ReplayStatus{}), fresh);
        if (st.kind != Step::Kind::Stay)
            return st;
    }
    if (level_ == Stage::Stopping)
        return Step::stay();
    if (s)
        checkpoints_(ctx, *s);
    if (!end_pushed_)
        stream_(ctx);
    return Step::stay();
}

IReplayStage::Step RunStage::tick_arming_(Ctx ctx, const ReplayStatus& s, bool fresh) noexcept {
    const ReplayPlay::Request& req = ctx.request;
    const ReplayPlay::ScannedMovie& scan = ctx.scan;
    const ReplayPlay::ArmPlan& arm = ctx.arm;
    if (level_ == Stage::Arming) {
        if (fresh && s.level == ReplayLevel::AwaitPowerOn) {

            CoreScope scope{};
            if (ctx.w.conf.sample_into(conf_) != 0)
                scope = CoreScope{conf_.gen};
            bool asked = false;
            if (arm.event == ReplayMsg::PowerOnEvent::ResetPulse
                && req.codec->core().rom_is_disc()
                && !disc_) {
                const proto::FileSlotDigit digit = req.codec->core().rom_slot_digit();
                UiRequest::MountImage mount{.index = digit.io_index(), .scope = scope};
                if (mount.path.assign(req.rom.view()) && ctx.w.asks.push(mount) != kUncaused) {
                    disc_ = ctx.w.clock.now();
                    return Step::stay();
                }
            } else if (disc_ && ctx.w.clock.now() - *disc_ < ReplayPlay::kDiscSettleNs) {
                return Step::stay();
            } else if (arm.event == ReplayMsg::PowerOnEvent::ResetPulse) {

                const UiRequest::ResetCore ask{
                    .edge = proto::ResetEdge::osd_toggle(proto::kResetBit, false),
                    .scope = scope
                };
                asked = ctx.w.asks.push(ask) != kUncaused;
            } else {

                const cores::RamImageRecipe ram = req.codec->core().power_on_ram(scan.facts);
                const std::size_t need = ram.size() == 0 ? 1 : 2;
                const bool image_asked = UiRequestRing::kSlots - ctx.w.asks.size() >= need
                    && (ram.size() == 0
                        || ctx.w.asks.push(UiRequest::LoadRamImage{.scope = scope, .recipe = ram})
                            != kUncaused);
                const proto::FileSlotDigit digit = req.codec->core().rom_slot_digit();
                UiRequest::LoadFileByDigit ask{};
                ask.digit = digit;
                ask.save = req.seeded_save ? UiRequest::SaveChoice::ReplaySeeded
                                           : UiRequest::SaveChoice::ReplayFresh;
                ask.scope = scope;
                (void)ask.path.assign(req.rom.view());
                asked = image_asked && ctx.w.asks.push(ask) != kUncaused;
            }
            if (!asked) {
                publish_(ctx, ReplayOp::Stop);
                level_ = Stage::Stopping;
                since_ = ctx.w.clock.now();
                return Step::stay();
            }
            level_ = Stage::Loading;
            since_ = ctx.w.clock.now();
            ctx.w.osd.close_osd();
        } else if (ctx.w.clock.now() - since_ > ReplayPlay::kAnswerNs) {
            publish_(ctx, ReplayOp::Stop);
            return finish_(ctx, ReplayStatus{.gen = ctx.run.gen, .end = ReplayEnd::Refused});
        }
        return Step::stay();
    }
    if (fresh && s.level == ReplayLevel::Running) {
        level_ = Stage::Streaming;
    } else if (ctx.w.clock.now() - since_ > ReplayPlay::kPowerOnNs) {
        ctx.w.diag.appendf("{\"t\":\"tas\",\"k\":\"no_power_on\",\"disc\":%u}", disc_ ? 1u : 0u);
        publish_(ctx, ReplayOp::Stop);
        level_ = Stage::Stopping;
        since_ = ctx.w.clock.now();
    }
    return Step::stay();
}

void RunStage::checkpoints_(Ctx ctx, const ReplayStatus& s) noexcept {
    ReplayPlay::ArmPlan& arm = ctx.arm;
    if (s.gen != ctx.run.gen)
        return;
    while (arm.next_check < arm.n_checks
           && s.movie_frame >= MovieFrame{}
           && s.movie_frame >= arm.checks[arm.next_check]) {
        char name[48];
        const int n = std::snprintf(
            name,
            sizeof name,
            "tas-%s-%s.png",
            ctx.run.gen.text().c_str(),
            arm.checks[arm.next_check].text().c_str()
        );
        if (n > 0)
            (void)ctx.w.shots.take(std::string_view(name, static_cast<std::size_t>(n)), false);
        ++arm.next_check;
    }
}

IReplayStage::Step RunStage::finish_(Ctx ctx, const ReplayStatus& s) noexcept {
    log_end(ctx.w.diag, s, ctx.run);
    if (s.end == ReplayEnd::EpochAmbiguous
        && ctx.run.tries + 1 < ReplayPlay::kEpochTries
        && level_ != Stage::Stopping) {
        ++ctx.run.tries;
        return arm_(ctx);
    }

    ctx.w.input.reset_joy_edges();
    return Step::done();
}

}
