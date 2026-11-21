// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/hd/hd_osd_main.h"
#include "app/hd/hd_osd_screen.h"
#include "app/hd/hd_osd_status.h"
#include "app/hps_framebuffer.h"
#include "app/video_pump.h"
#include "boot_parts.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/xthread/park_fds.h"
#include "infra/xthread/polled.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "open_verdict.h"
#include "os/clock.h"
#include "os/ui_vt_console.h"
#include "svc/config_snapshot.h"
#include "thread_assembly.h"

namespace mister::fw {

class HdOsdBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    HdOsdBlock(
        const BootParts& plat,
        const os::IClock& clock,
        app::HpsFramebuffer& fb,
        const app::VideoPump& video,
        OpenVerdict& verdict
    );
    HdOsdBlock(const HdOsdBlock&) = delete;
    HdOsdBlock& operator=(const HdOsdBlock&) = delete;

    [[nodiscard]] bool open_seat(
        const svc::ConfigSnapshot& c,
        ThreadAssembly::SeatMains& mains
    ) noexcept;

    [[nodiscard]] infra::OptRef<app::hd::HdOsdScreen> screen() noexcept;

    app::hd::HdOsdAskCell hd_ask_{};
    app::hd::HdOsdStatusCell hd_status_{};
    xthread::WakeFlag hd_wake_{};
    std::optional<xthread::ParkFds> hd_fds_{};
    app::hd::HdFlipChannel hd_flips_{xthread::Polled{}, hd_wake_};

    bool hd_raster_ = false;
    app::hd::HdRenderer hd_renderer_;

    os::UiVtConsole hd_vt_console_{};
    app::hd::HdOsdScreen hd_screen_;
    std::optional<app::hd::HdOsdMain> hd_main_;

private:
    std::optional<app::hd::HdRenderer::Frame> map_frame_(
        const BootParts& plat,
        OpenVerdict& verdict
    ) noexcept;
};

}
