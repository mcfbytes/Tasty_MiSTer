// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/cmd_fifo.h"
#include "app/cmd_verb_sink.h"
#include "app/hps_framebuffer.h"
#include "app/xml_kind.h"
#include "app/link_tx_channel.h"
#include "app/mgl_pump.h"
#include "app/recorder_control.h"
#include "app/replay_feeder.h"
#include "app/screenshot_pump.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "app/video_pump.h"
#include "infra/opt_ref.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"

namespace mister::app {

static_assert(kCmdLineMax <= VideoPump::kSpecMax, "a FIFO read's spec always fits the latch");

class CmdRouter : public ICmdVerbSink {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        infra::OptRef<MglPump> mgl{};
        VideoPump& video;
        infra::OptRef<HpsFramebuffer> fb{};
        ScreenshotPump& shots;
        ReplayFeeder& replay;
        RecorderControl& rec;
        UiRequestRing& ui_requests;
        xthread::RtStats& stats;
        LinkTxChannel& link_tx;
    };

    explicit CmdRouter(Wiring wiring) noexcept
        : mgl_(wiring.mgl), video_(wiring.video), fb_(wiring.fb), shots_(wiring.shots),
          replay_(wiring.replay), rec_(wiring.rec), ui_requests_(wiring.ui_requests),
          stats_(wiring.stats), link_tx_(wiring.link_tx) {}

    bool on(const CmdVerb::LoadCore& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        UiRequest::LoadCore req{};
        req.xml = v.kind == CmdVerb::RbfOrMra::Mra ? XmlKind::Mra : XmlKind::Rbf;
        if (!req.path.assign(v.path) || !ui_requests_.push(req)) {
            ++load_core_drops_;
        }
        return true;
    }

    bool on(const CmdVerb::Playlist& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return mgl_ && mgl_->take_playlist(v.path);
    }

    bool on(const CmdVerb::VideoMode& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return video_.take_video_mode(v.spec);
    }

    bool on(const CmdVerb::FbCmd& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        if (fb_) return fb_->take_fb_cmd(v.line);
        return video_.take_fb_cmd(v.line);
    }

    bool on(const CmdVerb::Screenshot& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return shots_.take(v.path, v.scaled);
    }

    bool on(const CmdVerb::Volume& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        if (!link_tx_.push(proto::LinkOp::SetVolume{.cmd = v.cmd, .arg = v.arg})) ++volume_drops_;
        return true;
    }

    bool on(const CmdVerb::RtStats&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        stats_.dump_requests.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool on(const CmdVerb::TasPlay& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return replay_.take_play(ReplayFeeder::Play{v.movie, v.rom, v.phase_us, v.lead});
    }
    bool on(const CmdVerb::TasStop&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return replay_.take_stop();
    }

    bool on(const CmdVerb::RecStart& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_.take_start(v.path, v.mode, v.opt);
    }
    bool on(const CmdVerb::RecArm& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_.take_arm(v.path, v.mode, v.opt);
    }
    bool on(const CmdVerb::RecStop&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_.take_stop();
    }
    bool on(const CmdVerb::RecDisarm&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_.take_disarm();
    }

    [[nodiscard]] std::uint32_t volume_drops() const noexcept { return volume_drops_; }

    [[nodiscard]] std::uint32_t load_core_drops() const noexcept { return load_core_drops_; }

private:
    infra::OptRef<MglPump> mgl_{};
    VideoPump& video_;
    infra::OptRef<HpsFramebuffer> fb_{};
    ScreenshotPump& shots_;
    ReplayFeeder& replay_;
    RecorderControl& rec_;
    UiRequestRing& ui_requests_;
    xthread::RtStats& stats_;
    LinkTxChannel& link_tx_;
    std::uint32_t volume_drops_ = 0;
    std::uint32_t load_core_drops_ = 0;
};

}  // namespace mister::app
