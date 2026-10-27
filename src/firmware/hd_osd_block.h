// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/hd_osd_main.h"
#include "app/hd_osd_screen.h"
#include "app/hd_osd_status.h"
#include "app/hps_framebuffer.h"
#include "app/video_pump.h"
#include "boot_parts.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
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

    [[nodiscard]] infra::OptRef<app::HdOsdScreen> screen() noexcept;

    app::HdOsdAskCell hd_ask_{};
    app::HdOsdStatusCell hd_status_{};
    xthread::WakeFlag hd_wake_{};
    std::optional<xthread::ParkFds> hd_fds_{};
    app::HdFlipChannel hd_flips_{xthread::Polled{}, xthread::Polled{}};

    bool hd_raster_ = false;
    app::HdRenderer hd_renderer_;

    os::UiVtConsole hd_vt_console_{};
    app::HdOsdScreen hd_screen_;
    std::optional<app::HdOsdMain> hd_main_;

private:
    std::optional<app::HdRenderer::Frame> map_frame_(
        const BootParts& plat,
        OpenVerdict& verdict
    ) noexcept;
};

}
