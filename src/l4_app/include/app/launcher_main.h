// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/launcher_host.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"

namespace mister::app {

class LauncherMain final : public xthread::SeatMain<LauncherMain> {
    TASTY_SEAT_RESIDENT(Launcher);

public:
    LauncherMain(LauncherHost& host, xthread::ParkFds fds,
                 infra::OptRef<xthread::WakeFlag> asker = {}) noexcept
        : host_(host), wake_(fds.wake()), stop_(std::move(fds).take_stop()),
          pause_(wake_, asker ? &*asker : nullptr), park_(wake_, stop_, host_.poll_fd()) {}
    LauncherMain(const LauncherMain&) = delete;
    LauncherMain& operator=(const LauncherMain&) = delete;
    LauncherMain(LauncherMain&&) = delete;
    LauncherMain& operator=(LauncherMain&&) = delete;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept { host_.serve(); }
    [[nodiscard]] bool idle() const noexcept { return host_.idle(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return host_.park_ms(); }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }
    [[nodiscard]] bool paused() noexcept { return pause_.observe(*this); }

    [[nodiscard]] bool pause_pending() const noexcept {
        return pause_.pending() && (pause_.asked() != taken_ || host_.quiescent());
    }
    void settle() noexcept {}

    void on_pause() noexcept {
        taken_ = pause_.asked();
        host_.pause();
    }
    void on_resume() noexcept {
        taken_ = 0;
        host_.resume();
    }
    [[nodiscard]] bool quiescent() const noexcept { return host_.quiescent(); }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

private:
    LauncherHost& host_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::PauseLatch pause_;
    xthread::SeatPark park_;
    xthread::PauseGen taken_ = 0;
};

static_assert(xthread::SeatBody<LauncherMain>);

}  // namespace mister::app
