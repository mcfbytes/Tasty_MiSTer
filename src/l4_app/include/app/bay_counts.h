// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "hal/entry_mask.h"
#include "hal/reset_counts.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/telemetry.h"
#include "infra/prelude.h"

namespace mister::app {

struct BayCounts {
    TASTY_SEAT_RESIDENT(Main);
    std::uint32_t builds = 0;
    std::uint32_t bay_full = 0;
    std::uint32_t frees = 0;
    std::uint32_t window_refusals = 0;
    std::uint32_t pad_grants = 0;
    std::uint32_t pad_refusals = 0;
    std::uint32_t lw_undiscovered_refusals = 0;
    std::uint32_t lw_firewall_held_refusals = 0;
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
    infra::Tally enables = {};
    infra::Tally enable_refusals = {};
    infra::Tally open_refusals = {};
    infra::Tally enable_lost = {};
    hal::EntryMask owned_last{};
    hal::ResetCounts resets_last{};
};

static_assert(std::is_trivially_copyable_v<BayCounts>);
using BayCountsCell = xthread::Telemetry<BayCounts, SeatTag::Main>;

}
