// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/never_park.h"
#include "infra/wake_flag.h"
#include "reactor/frame_clock.h"

namespace mister::reactor {

class FrameMain final : public xthread::SeatMain<FrameMain> {
    TASTY_SEAT_RESIDENT(Frame);

public:
    explicit FrameMain(FrameClock& clock) noexcept : clock_(clock) {}
    FrameMain(const FrameMain&) = delete;
    FrameMain& operator=(const FrameMain&) = delete;
    FrameMain(FrameMain&&) = delete;
    FrameMain& operator=(FrameMain&&) = delete;

    [[nodiscard]] FrameClock& clock() noexcept { return clock_; }

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return true; }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return -1; }
    [[nodiscard]] xthread::NeverPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }
    [[nodiscard]] bool pause_pending() const noexcept { return false; }
    void settle() noexcept {}

private:
    FrameClock& clock_;
    xthread::WakeFlag stop_{};
    xthread::NeverPark park_{};
};

static_assert(xthread::SeatBody<FrameMain>);

}  // namespace mister::reactor
