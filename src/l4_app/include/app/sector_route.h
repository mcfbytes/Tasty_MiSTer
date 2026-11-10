// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "hal/types.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/types.h"

namespace mister::app {

struct SectorRoute {
    bool leased = false;
    bool on_lw = false;
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

    hal::EventCount8 fab_rejected{};
    hal::EventCount8 fab_skipped{};
    hal::EventCount8 fab_aborted{};
    hal::EventCount8 fab_reset_fills{};

    proto::BlockSlotMask cards_on_lw{};
    std::uint32_t card_reads = 0;
    std::uint32_t card_writes = 0;
    std::uint32_t card_blanks = 0;
    std::uint32_t card_creates = 0;
    std::uint32_t card_discards = 0;
    std::uint32_t card_fails = 0;
    std::uint32_t card_misses = 0;
    std::uint32_t card_lat_max_us = 0;
    std::uint16_t bind_gen = 0;
};

static_assert(std::is_trivially_copyable_v<SectorRoute>);
using SectorRouteCell = xthread::Telemetry<SectorRoute, SeatTag::Io>;

}
