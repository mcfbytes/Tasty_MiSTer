// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/hd_renderer.h"
#include "infra/error.h"
#include "infra/park_fds.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"

namespace mister::app {

class HdOsdMain final : public xthread::SeatMain<HdOsdMain> {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    HdOsdMain(HdRenderer& renderer, xthread::ParkFds fds) noexcept
        : renderer_(renderer), wake_(fds.wake()), stop_(std::move(fds).take_stop()),
          park_(wake_, stop_) {}
    HdOsdMain(const HdOsdMain&) = delete;
    HdOsdMain& operator=(const HdOsdMain&) = delete;
    HdOsdMain(HdOsdMain&&) = delete;
    HdOsdMain& operator=(HdOsdMain&&) = delete;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return stopping() || renderer_.idle(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return renderer_.park_ms(); }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }
    [[nodiscard]] bool pause_pending() const noexcept { return false; }
    void settle() noexcept { renderer_.release(); }

private:
    HdRenderer& renderer_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<HdOsdMain>);

}  // namespace mister::app
