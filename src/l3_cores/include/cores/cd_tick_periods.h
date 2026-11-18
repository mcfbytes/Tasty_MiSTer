// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "cores/cd_speed_count.h"
#include "infra/indexed_array.h"

namespace mister::cores {

struct CdTickPeriods {

    static constexpr std::size_t kMaxSpeeds = 4;

    std::chrono::microseconds idle;
    std::chrono::microseconds stream;

    std::chrono::microseconds caller;
    CdSpeedCount speeds;

    std::array<std::chrono::microseconds, kMaxSpeeds> speed{};

    [[nodiscard]] constexpr std::chrono::microseconds period(
        bool active,
        CdSpeedIndex speed_index
    ) const noexcept {
        if (!active)
            return idle;
        if (speeds.empty())
            return stream;
        const CdSpeedCount live = std::min(speeds, kMaxSpeedCount);
        const CdSpeedIndex bounded = live.clamp(speed_index);
        const std::chrono::microseconds override_period = infra::at_index(speed, bounded);
        return override_period == std::chrono::microseconds::zero() ? stream : override_period;
    }
};

static_assert(kMaxSpeedCount == CdSpeedCount{CdTickPeriods::kMaxSpeeds});

}
