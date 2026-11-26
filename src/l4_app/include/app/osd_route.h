// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/cell_gen.h"
#include "infra/tally.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

struct OsdRoute {
    bool bound = false;
    bool on_ocram = false;
    infra::Tally ring_lost = {};
    std::uint32_t frames = 0;
    std::uint32_t rows = 0;
    std::uint32_t revoked = 0;
    std::uint32_t restores = 0;
};

static_assert(std::is_trivially_copyable_v<OsdRoute>);
using OsdRouteCell = xthread::Telemetry<OsdRoute, SeatTag::Ui>;

using OsdLeaseCell = xthread::Telemetry<infra::CellGen, SeatTag::Ui>;

}
