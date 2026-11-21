// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/capture/frame_hasher.h"
#include "infra/error.h"
#include "infra/xthread/park_fds.h"
#include "infra/park_timeout.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/seat_main.h"
#include "infra/xthread/seat_park.h"
#include "infra/xthread/wake_flag.h"
#include "os/delay.h"

namespace mister::app::capture {

class EncodeMain final : public xthread::SeatMain<EncodeMain> {
    TASTY_SEAT_RESIDENT(Encode);

public:
    EncodeMain(FrameHasher& hasher, xthread::ParkFds fds, os::IDelay& delay) noexcept
        : hasher_(hasher),
          wake_(fds.wake()),
          stop_(std::move(fds).take_stop()),
          park_(wake_, stop_),
          delay_(delay) {}

    EncodeMain(const EncodeMain&) = delete;
    EncodeMain& operator=(const EncodeMain&) = delete;
    EncodeMain(EncodeMain&&) = delete;
    EncodeMain& operator=(EncodeMain&&) = delete;

    void start() noexcept;

    void stop() noexcept { stop_.request(); }

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept {
        return stopping() ? hasher_.drained() : hasher_.idle();
    }

    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept { return hasher_.park_ms(); }

    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }

    [[nodiscard]] bool pause_pending() const noexcept { return false; }

    void settle() noexcept { hasher_.release(); }

private:
    FrameHasher& hasher_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::SeatPark park_;
    os::IDelay& delay_;
};

static_assert(xthread::SeatBody<EncodeMain>);

}
