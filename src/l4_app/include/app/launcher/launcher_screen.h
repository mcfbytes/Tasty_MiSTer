// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/fb_tenancy.h"
#include "app/launcher/launcher_fb_ack.h"
#include "app/launcher_state.h"
#include "app/osd_close.h"
#include "app/screen_gen.h"
#include "app/video_pump_stats.h"
#include "infra/xthread/coworker.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"

namespace mister::app {

class HpsFramebuffer;

}

namespace mister::app::launcher {

class LauncherScreen : public xthread::ICoworker {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        const LauncherStateCell& state;
        LauncherFbAckCell& fb_ack;
        xthread::WakeFlag& launcher_wake;
        HpsFramebuffer& fb;
        const VideoStatsCell& video_stats;
        IOsdClose& osd;
    };

    explicit LauncherScreen(const Wiring& w) noexcept : w_(w) {}

    LauncherScreen(const LauncherScreen&) = delete;
    LauncherScreen& operator=(const LauncherScreen&) = delete;

    void serve() noexcept override;

    [[nodiscard]] std::uint32_t raises() const noexcept { return raises_; }

private:
    [[nodiscard]] bool raise_() noexcept;

    void drop_lease_() noexcept;

    void retry_drop_() noexcept;

    Wiring w_;
    LauncherStateCell::Reader seen_{};
    LauncherState last_{};
    std::uint32_t applies_seen_ = 0;
    std::uint32_t raises_ = 0;
    ScreenGen acked_gen_{};
    std::optional<FbLease> lease_{};
    bool raise_owed_ = false;
    bool drop_owed_ = false;
    bool ack_owed_ = false;
};

}
