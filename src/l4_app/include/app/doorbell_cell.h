// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct DoorbellStats {
    std::uint32_t declared = 0;
    std::uint32_t bound = 0;
    std::uint32_t refusals = 0;
    std::uint32_t fallbacks = 0;
    std::uint32_t retirements = 0;
};
static_assert(std::is_trivially_copyable_v<DoorbellStats>);

using DoorbellStatsCell = xthread::Telemetry<DoorbellStats, SeatTag::RT>;

}  // namespace mister::app
