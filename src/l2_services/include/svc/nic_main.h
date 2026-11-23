// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/opt_ref.h"
#include "infra/park_timeout.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/park_fds.h"
#include "infra/xthread/pause_latch.h"
#include "infra/xthread/seat_main.h"
#include "infra/xthread/seat_park.h"
#include "infra/xthread/wake_flag.h"
#include "svc/nic_bridge.h"

namespace mister::svc {

class NicMain final : public xthread::SeatMain<NicMain> {
    TASTY_SEAT_RESIDENT(Nic);

public:
    NicMain(
        NicBridge& bridge,
        xthread::ParkFds fds,
        infra::OptRef<xthread::WakeFlag> asker = {}
    ) noexcept
        : bridge_(bridge),
          wake_(fds.wake()),
          stop_(std::move(fds).take_stop()),
          pause_(wake_, asker),
          park_(wake_, stop_) {}

    NicMain(const NicMain&) = delete;
    NicMain& operator=(const NicMain&) = delete;
    NicMain(NicMain&&) = delete;
    NicMain& operator=(NicMain&&) = delete;

    void start() noexcept;

    void stop() noexcept { stop_.request(); }

    void serve() noexcept { bridge_.serve(); }

    [[nodiscard]] bool idle() const noexcept { return stopping() || bridge_.idle(); }

    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept { return bridge_.park_ms(); }

    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return pause_.observe(pause_seen_, *this); }

    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }

    void settle() noexcept { bridge_.stop_card(); }

    void on_pause() noexcept { bridge_.stop_card(); }

    void on_resume() noexcept {}

    [[nodiscard]] bool quiescent() const noexcept { return !bridge_.up(); }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

private:
    NicBridge& bridge_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::PauseLatch pause_;
    xthread::PauseLatch::Observer pause_seen_{};
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<NicMain>);

}
