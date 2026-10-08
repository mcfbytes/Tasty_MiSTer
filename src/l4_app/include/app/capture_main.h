// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/frame_copier.h"
#include "infra/error.h"
#include "infra/park_fds.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"

namespace mister::app {

class CaptureMain final : public xthread::SeatMain<CaptureMain> {
    TASTY_SEAT_RESIDENT(Capture);

public:
    CaptureMain(FrameCopier& copier, xthread::ParkFds fds) noexcept
        : copier_(copier), wake_(fds.wake()), stop_(std::move(fds).take_stop()),
          park_(wake_, stop_) {}
    CaptureMain(const CaptureMain&) = delete;
    CaptureMain& operator=(const CaptureMain&) = delete;
    CaptureMain(CaptureMain&&) = delete;
    CaptureMain& operator=(CaptureMain&&) = delete;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return stopping() || copier_.idle(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return copier_.park_ms(); }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }
    [[nodiscard]] bool pause_pending() const noexcept { return false; }
    void settle() noexcept { copier_.release(); }

private:
    FrameCopier& copier_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<CaptureMain>);

}  // namespace mister::app
