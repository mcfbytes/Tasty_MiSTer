// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct BayCounts {
    std::uint32_t builds = 0;
    std::uint32_t bay_full = 0;
    std::uint32_t frees = 0;
    std::uint32_t window_refusals = 0;
    std::uint32_t pad_grants = 0;
    std::uint32_t pad_refusals = 0;
    std::uint32_t sector_grants = 0;
    std::uint32_t sector_refusals = 0;
    std::uint32_t lease_holds = 0;
    std::uint32_t lease_long_holds = 0;
    std::uint32_t osd_grants = 0;
    std::uint32_t osd_refusals = 0;
    std::uint32_t card_grants = 0;
    std::uint32_t card_refusals = 0;
    std::uint32_t bridge_refusals = 0;
    std::uint32_t sys_grants = 0;
    std::uint32_t sys_refusals = 0;
    std::uint32_t sys_pin_refusals = 0;
    std::uint32_t poll_retirements = 0;
};

static_assert(std::is_trivially_copyable_v<BayCounts>);
using BayCountsCell = xthread::Telemetry<BayCounts, SeatTag::Main>;

}
