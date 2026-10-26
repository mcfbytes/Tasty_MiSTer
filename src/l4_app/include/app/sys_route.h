// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct SysRoute {
    std::uint32_t leased = 0;
    std::uint32_t on_lw = 0;
    std::uint32_t drains = 0;
    std::uint32_t snapshots = 0;
    std::uint32_t retries = 0;
    std::uint32_t torn = 0;
    std::uint32_t spurious = 0;
    std::uint32_t revoked = 0;
    std::uint32_t bind_refusals = 0;
    std::uint32_t lat_max_us = 0;
    std::uint16_t bind_gen = 0;
};

static_assert(std::is_trivially_copyable_v<SysRoute>);
using SysRouteCell = xthread::Telemetry<SysRoute, SeatTag::Io>;

}
