// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/cmd_fifo.h"
#include "app/cmd_verb_sink.h"
#include "app/xml_kind.h"
#include "app/link_tx_channel.h"
#include "app/mgl_pump.h"
#include "app/recorder_control.h"
#include "app/replay_feeder.h"
#include "app/screenshot_pump.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "app/video_pump.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"

namespace mister::app {

static_assert(kCmdLineMax <= VideoPump::kSpecMax, "a FIFO read's spec always fits the latch");

class CmdRouter : public ICmdVerbSink {
    TASTY_SEAT_RESIDENT(Ui);

public:
    CmdRouter(MglPump* mgl, VideoPump* video) noexcept : mgl_(mgl), video_(video) {}

    bool on(const CmdVerb::LoadCore& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        if (ui_requests_ == nullptr) return false;
        UiRequest::LoadCore req{};
        req.xml = v.kind == CmdVerb::RbfOrMra::Mra ? XmlKind::Mra : XmlKind::Rbf;
        if (!req.path.assign(v.path) || !ui_requests_->push(req)) {
            ++load_core_drops_;
        }
        return true;
    }

    bool on(const CmdVerb::Playlist& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return mgl_ != nullptr && mgl_->take_playlist(v.path);
    }

    bool on(const CmdVerb::VideoMode& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return video_ != nullptr && video_->take_video_mode(v.spec);
    }

    bool on(const CmdVerb::FbCmd& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return video_ != nullptr && video_->take_fb_cmd(v.line);
    }

    bool on(const CmdVerb::Screenshot& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return shots_ != nullptr && shots_->take(v.path, v.scaled);
    }

    bool on(const CmdVerb::Volume& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        if (link_tx_ == nullptr) return false;
        if (!link_tx_->push(proto::LinkOp::SetVolume{.cmd = v.cmd, .arg = v.arg})) ++volume_drops_;
        return true;
    }

    bool on(const CmdVerb::RtStats&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        if (stats_ == nullptr) return false;
        stats_->dump_requests.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool on(const CmdVerb::TasPlay& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return replay_ != nullptr &&
               replay_->take_play(ReplayFeeder::Play{v.movie, v.rom, v.phase_us, v.lead});
    }
    bool on(const CmdVerb::TasStop&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return replay_ != nullptr && replay_->take_stop();
    }

    bool on(const CmdVerb::RecStart& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_ != nullptr && rec_->take_start(v.path, v.mode);
    }
    bool on(const CmdVerb::RecArm& v) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_ != nullptr && rec_->take_arm(v.path, v.mode);
    }
    bool on(const CmdVerb::RecStop&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_ != nullptr && rec_->take_stop();
    }
    bool on(const CmdVerb::RecDisarm&) noexcept override {
        TASTY_SEAT_BODY(CmdRouter);
        return rec_ != nullptr && rec_->take_disarm();
    }

    void install(CmdFifo& fifo) noexcept { fifo.set_route(this); }

    void bind_recorder(RecorderControl* rec) noexcept { rec_ = rec; }

    void bind_replay(ReplayFeeder* feeder) noexcept { replay_ = feeder; }

    void bind_ui_requests(UiRequestRing* ring) noexcept { ui_requests_ = ring; }

    void bind_rt_stats(xthread::RtStats* stats) noexcept { stats_ = stats; }

    void bind_screenshots(ScreenshotPump* pump) noexcept { shots_ = pump; }

    void bind_link_tx(LinkTxChannel* tx) noexcept { link_tx_ = tx; }

    [[nodiscard]] std::uint32_t volume_drops() const noexcept { return volume_drops_; }

    [[nodiscard]] std::uint32_t load_core_drops() const noexcept { return load_core_drops_; }

private:
    MglPump* mgl_;
    VideoPump* video_;
    ScreenshotPump* shots_ = nullptr;
    ReplayFeeder* replay_ = nullptr;
    RecorderControl* rec_ = nullptr;
    UiRequestRing* ui_requests_ = nullptr;
    xthread::RtStats* stats_ = nullptr;
    LinkTxChannel* link_tx_ = nullptr;
    std::uint32_t volume_drops_ = 0;
    std::uint32_t load_core_drops_ = 0;
};

}  // namespace mister::app
