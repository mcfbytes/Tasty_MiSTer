// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "cores/cd_tick_periods.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::cores {

class CdCadenceGate {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdCadenceGate(const CdTickPeriods& ticks, const os::IClock& c) noexcept
        : ticks_(ticks),
          clock_(c) {}

    [[nodiscard]] bool admit(bool active, CdSpeedIndex speed_index) noexcept;

    [[nodiscard]] std::uint32_t gated_ticks() const noexcept { return gated_ticks_; }

private:
    CdTickPeriods ticks_;
    const os::IClock& clock_;

    std::optional<os::RawMonotonicClock::time_point> next_due_{};
    std::uint32_t gated_ticks_ = 0;
};

}
