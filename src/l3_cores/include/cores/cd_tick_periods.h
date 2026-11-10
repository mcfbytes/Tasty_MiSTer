// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "cores/cd_speed_index.h"

namespace mister::cores {

struct CdTickPeriods {

    static constexpr std::size_t kMaxSpeeds = 4;

    std::chrono::microseconds idle;
    std::chrono::microseconds stream;

    std::chrono::microseconds caller;
    std::uint8_t speeds;

    std::array<std::chrono::microseconds, kMaxSpeeds> speed{};

    [[nodiscard]] constexpr std::chrono::microseconds period(
        bool active,
        CdSpeedIndex speed_index
    ) const noexcept {
        if (!active)
            return idle;
        if (speeds == 0)
            return stream;
        const std::size_t live = speeds < kMaxSpeeds ? speeds : kMaxSpeeds;
        const std::size_t bounded = speed_index.v < live ? speed_index.v : live - 1;
        const std::chrono::microseconds override_period = speed[bounded];
        return override_period == std::chrono::microseconds::zero() ? stream : override_period;
    }
};

}
