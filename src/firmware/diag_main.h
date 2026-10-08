// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "diag_sampler.h"
#include "infra/error.h"
#include "infra/park_fds.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"
#include "hal/thread_map.h"

namespace mister::fw {

inline constexpr unsigned kDiagSliceMs = 50;

class DiagMain final : public xthread::SeatMain<DiagMain> {
    TASTY_SEAT_RESIDENT(Diag);

public:
    static constexpr SeatTag kSeat = SeatTag::Diag;

    DiagMain(DiagSampler& sampler, xthread::ParkFds fds) noexcept
        : sampler_(sampler), wake_(fds.wake()), stop_(std::move(fds).take_stop()),
          park_(wake_, stop_) {}
    DiagMain(const DiagMain&) = delete;
    DiagMain& operator=(const DiagMain&) = delete;
    DiagMain(DiagMain&&) = delete;
    DiagMain& operator=(DiagMain&&) = delete;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return true; }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return static_cast<int>(rest_ms_); }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }
    [[nodiscard]] bool pause_pending() const noexcept { return false; }
    void settle() noexcept;

private:
    DiagSampler& sampler_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::SeatPark park_;
    std::uint32_t slice_ = 0;
    unsigned rest_ms_ = 0;
    bool final_sampled_ = false;
};

static_assert(xthread::SeatBody<DiagMain>);

}  // namespace mister::fw
