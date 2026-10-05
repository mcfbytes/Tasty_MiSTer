// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/launcher_fb_ack.h"
#include "app/launcher_state.h"
#include "app/osd_close.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

class HpsFramebuffer;
class VideoPump;

class LauncherScreen {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        const LauncherStateCell* state = nullptr;
        LauncherFbAckCell* fb_ack = nullptr;
        xthread::WakeFlag* launcher_wake = nullptr;
        HpsFramebuffer* fb = nullptr;
        const VideoPump* video = nullptr;
        IOsdClose* osd = nullptr;
    };

    explicit LauncherScreen(const Wiring& w) noexcept : w_(w) {}
    LauncherScreen(const LauncherScreen&) = delete;
    LauncherScreen& operator=(const LauncherScreen&) = delete;

    void tick() noexcept;

    [[nodiscard]] std::uint32_t raises() const noexcept { return raises_; }

private:
    [[nodiscard]] bool raise_() noexcept;

    Wiring w_;
    LauncherStateCell::Reader seen_{};
    LauncherState last_{};
    std::uint32_t applies_seen_ = 0;
    std::uint32_t raises_ = 0;
    std::uint32_t acked_gen_ = 0;
    bool raise_owed_ = false;
    bool ack_owed_ = false;
};

}  // namespace mister::app
