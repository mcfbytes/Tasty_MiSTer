// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct OsdRoute {
    std::uint32_t leased = 0;
    std::uint32_t on_lw = 0;
    std::uint32_t frames = 0;
    std::uint32_t rows = 0;
    std::uint32_t revoked = 0;
    std::uint32_t ctrl = 0;
};

static_assert(std::is_trivially_copyable_v<OsdRoute>);
using OsdRouteCell = xthread::Telemetry<OsdRoute, SeatTag::Ui>;

}
