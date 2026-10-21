// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/run_stage.h"

#include <cstdio>
#include <string_view>

#include "app/input_wire.h"
#include "app/osd_close.h"
#include "app/screenshot_pump.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "infra/diag_log.h"
#include "infra/message_sum.h"

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
    since_ns_ = ctx.now_ns();
    return Step::stay();
}

std::optional<ReplayStatus> RunStage::sample_status_(Ctx ctx) const noexcept {
    ReplayStatus s{};
    if (ctx.w.status.sample_into(s) == 0)
        return std::nullopt;
    return s;
}

void RunStage::publish_(Ctx ctx, ReplayOp op) noexcept {
    ctx.w.control.publish(ReplayControl{.gen = ctx.play.gen, .op = op});
}

IReplayStage::Step RunStage::arm_(Ctx ctx) noexcept {
    ReplayPlay& play = ctx.play;
    ++play.gen;
    if (play.gen == 0)
        play.gen = 1;
    next_run_ = 0;
    end_pushed_ = false;
    play.frames = 0;

    publish_(ctx, ReplayOp::Play);
    const cores::IMovieCodec::Raster r = play.codec->raster(play.facts);
    const ReplayMsg::Arm a{
        .ports = play.facts.ports,
        .rom_index = play.rom_index,
        .vsync_ok = static_cast<std::uint8_t>(play.vsync_ok ? 1 : 0),
        .p0 = play.p0,
        .lead = static_cast<std::int16_t>(play.lead),
        .event = play.event,
        .late_frames = play.codec->power_on(play.facts).late_frames,
        .offset_us = play.offset_us,
        .period_ns = r.period_ns,
        .line0_ns = r.line0_ns,
        .poweron_ns = play.poweron_ns
    };
    if (!ctx.w.ring.push(infra::make<ReplayMsg>(a, ReplayMsg::Head{play.gen}))) {
        publish_(ctx, ReplayOp::Stop);
        return Step::refuse(Refusal::Busy);
    }
    level_ = Stage::Arming;
    since_ns_ = ctx.now_ns();
    return Step::stay();
}

void RunStage::stream_(Ctx ctx) noexcept {
    ReplayPlay& play = ctx.play;
    while (next_run_ < play.runs.size()) {
        const ReplayMsg::Input& run = play.runs[next_run_];
        if (!ctx.w.ring.push(infra::make<ReplayMsg>(run, ReplayMsg::Head{play.gen})))
            return;
        play.frames = run.last + 1;
        ++next_run_;
    }
    end_pushed_ = ctx.w.ring.push(
        infra::make<ReplayMsg>(
            ReplayMsg::End{.frame = play.movie_frames},
            ReplayMsg::Head{play.gen}
        )
    );
}

IReplayStage::Step RunStage::tick(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(RunStage);
    const auto s = sample_status_(ctx);
    const bool fresh = s && s->gen == ctx.play.gen;
    if (fresh && s->level == ReplayLevel::Idle && s->end != ReplayEnd::None)
        return finish_(ctx, *s);
    if (level_ == Stage::Stopping) {
        if (ctx.now_ns() - since_ns_ > ReplayPlay::kAnswerNs)
            return finish_(ctx, ReplayStatus{.gen = ctx.play.gen, .end = ReplayEnd::Stopped});
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
    ReplayPlay& play = ctx.play;
    if (level_ == Stage::Arming) {
        if (fresh && s.level == ReplayLevel::AwaitPowerOn) {

            CoreScope scope{};
            if (ctx.w.conf.sample_into(conf_) != 0)
                scope = CoreScope{conf_.gen};
            bool asked = false;
            if (play.event == ReplayMsg::PowerOnEvent::ResetPulse
                && play.codec->rom_is_disc()
                && !disc_ns_) {
                UiRequest::MountImage mount{
                    .index = proto::IoIndex{play.codec->rom_digit()},
                    .scope = scope
                };
                if (mount.path.assign(play.rom.view()) && ctx.w.asks.push(mount) != kUncaused) {
                    disc_ns_ = ctx.now_ns();
                    return Step::stay();
                }
            } else if (disc_ns_ && ctx.now_ns() - *disc_ns_ < ReplayPlay::kDiscSettleNs) {
                return Step::stay();
            } else if (play.event == ReplayMsg::PowerOnEvent::ResetPulse) {

                const UiRequest::ResetCore ask{
                    .edge = proto::ResetEdge::osd_toggle(proto::StatusBit{0}, false),
                    .scope = scope
                };
                asked = ctx.w.asks.push(ask) != kUncaused;
            } else {

                const cores::RamImageRecipe ram = play.codec->power_on_ram(play.facts);
                const std::size_t need = ram.size() == 0 ? 1 : 2;
                const bool image_asked = UiRequestRing::kSlots - ctx.w.asks.size() >= need
                    && (ram.size() == 0
                        || ctx.w.asks.push(UiRequest::LoadRamImage{.scope = scope, .recipe = ram})
                            != kUncaused);
                UiRequest::LoadFileByDigit ask{};
                ask.digit = proto::FileSlotDigit{play.codec->rom_digit()};
                ask.save = play.seeded_save ? UiRequest::SaveChoice::ReplaySeeded
                                            : UiRequest::SaveChoice::ReplayFresh;
                ask.scope = scope;
                (void)ask.path.assign(play.rom.view());
                asked = image_asked && ctx.w.asks.push(ask) != kUncaused;
            }
            if (!asked) {
                publish_(ctx, ReplayOp::Stop);
                level_ = Stage::Stopping;
                since_ns_ = ctx.now_ns();
                return Step::stay();
            }
            level_ = Stage::Loading;
            since_ns_ = ctx.now_ns();
            ctx.w.osd.close_osd();
        } else if (ctx.now_ns() - since_ns_ > ReplayPlay::kAnswerNs) {
            publish_(ctx, ReplayOp::Stop);
            return finish_(ctx, ReplayStatus{.gen = play.gen, .end = ReplayEnd::Refused});
        }
        return Step::stay();
    }
    if (fresh && s.level == ReplayLevel::Running) {
        level_ = Stage::Streaming;
    } else if (ctx.now_ns() - since_ns_ > ReplayPlay::kPowerOnNs) {
        ctx.w.diag.appendf("{\"t\":\"tas\",\"k\":\"no_power_on\",\"disc\":%u}", disc_ns_ ? 1u : 0u);
        publish_(ctx, ReplayOp::Stop);
        level_ = Stage::Stopping;
        since_ns_ = ctx.now_ns();
    }
    return Step::stay();
}

void RunStage::checkpoints_(Ctx ctx, const ReplayStatus& s) noexcept {
    ReplayPlay& play = ctx.play;
    if (s.gen != play.gen)
        return;
    while (play.next_check < play.n_checks
           && s.movie_frame >= 0
           && static_cast<std::uint32_t>(s.movie_frame) >= play.checks[play.next_check]) {
        char name[48];
        const int n = std::snprintf(
            name,
            sizeof name,
            "tas-%u-%u.png",
            static_cast<unsigned>(play.gen),
            play.checks[play.next_check]
        );
        if (n > 0)
            (void)ctx.w.shots.take(std::string_view(name, static_cast<std::size_t>(n)), false);
        ++play.next_check;
    }
}

IReplayStage::Step RunStage::finish_(Ctx ctx, const ReplayStatus& s) noexcept {
    ReplayPlay& play = ctx.play;
    play.log_end(ctx.w.diag, s);
    if (s.end == ReplayEnd::EpochAmbiguous
        && play.tries + 1 < ReplayPlay::kEpochTries
        && level_ != Stage::Stopping) {
        ++play.tries;
        return arm_(ctx);
    }

    ctx.w.input.publish_edge_reset();
    return Step::done();
}

}
