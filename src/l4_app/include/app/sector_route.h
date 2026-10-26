// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct SectorRoute {
    std::uint32_t leased = 0;
    std::uint32_t on_lw = 0;
    std::uint32_t served = 0;
    std::uint32_t resends = 0;
    std::uint32_t corrupted = 0;
    std::uint32_t begin_lost = 0;
    std::uint32_t drops = 0;
    std::uint32_t spurious = 0;
    std::uint32_t revoked = 0;
    std::uint32_t read_ahead = 0;
    std::uint32_t lat_max_us = 0;
    std::uint32_t bind_refusals = 0;

    std::uint32_t fab_rejected = 0;
    std::uint32_t fab_skipped = 0;
    std::uint32_t fab_aborted = 0;
    std::uint32_t fab_reset_fills = 0;

    std::uint32_t cards_on_lw = 0;
    std::uint32_t card_reads = 0;
    std::uint32_t card_writes = 0;
    std::uint32_t card_blanks = 0;
    std::uint32_t card_creates = 0;
    std::uint32_t card_discards = 0;
    std::uint32_t card_fails = 0;
    std::uint32_t card_lat_max_us = 0;
    std::uint16_t bind_gen = 0;
};

static_assert(std::is_trivially_copyable_v<SectorRoute>);
using SectorRouteCell = xthread::Telemetry<SectorRoute, SeatTag::Io>;

}
