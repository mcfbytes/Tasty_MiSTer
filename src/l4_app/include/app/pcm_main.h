// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/pcm_ring_feeder.h"
#include "infra/error.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"

namespace mister::app {

class PcmMain final : public xthread::SeatMain<PcmMain> {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    explicit PcmMain(PcmRingFeeder& feeder) noexcept;
    PcmMain(const PcmMain&) = delete;
    PcmMain& operator=(const PcmMain&) = delete;
    PcmMain(PcmMain&&) = delete;
    PcmMain& operator=(PcmMain&&) = delete;

    [[nodiscard]] Ex<void> open() noexcept;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return feeder_.rest_ms(); }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return *park_; }
    [[nodiscard]] bool paused() noexcept { return pause_.observe(*this); }
    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }
    void settle() noexcept { feeder_.release(); }

    void on_pause() noexcept;
    void on_resume() noexcept {}
    [[nodiscard]] bool quiescent() const noexcept { return true; }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

    [[nodiscard]] std::uint32_t paused_cmd_drops() const noexcept { return paused_cmd_drops_; }

private:
    [[nodiscard]] bool inboxes_empty_() const noexcept { return cmds_.empty(); }

    PcmRingFeeder& feeder_;
    xthread::WakeFlag wake_{};
    xthread::WakeFlag stop_{};
    PcmRingFeeder::Commands cmds_{wake_};
    xthread::PauseLatch pause_{wake_};
    std::uint32_t paused_cmd_drops_ = 0;
    std::optional<xthread::SeatPark> park_{};
};

static_assert(xthread::SeatBody<PcmMain>);

}  // namespace mister::app
