// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/cell_gen.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/telemetry.h"
#include "os/clock.h"

namespace mister::app {

struct PadRoute {

    struct Stamp {
        os::MonoInstant at{};

        constexpr Stamp() noexcept : at() {}
    };

    bool bound = false;
    infra::Tally stores{};
    infra::Tally binds{};
    infra::Tally revoked{};
    infra::Tally gated_stores{};
    infra::Tally gated_drops{};
    infra::Tally relay_seq{};
    Stamp relay_read{};
    infra::Tally events_refused{};
    infra::Tally events_held{};
    infra::Tally events_lost{};
};

static_assert(std::is_trivially_copyable_v<PadRoute>);
using PadRouteCell = xthread::Telemetry<PadRoute, SeatTag::Input>;

using PadLeaseCell = xthread::Telemetry<infra::CellGen, SeatTag::Input>;

}
