// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <type_traits>

#include "cores/switch_table.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/types.h"

namespace mister::app {

struct DipChoices {
    TASTY_SEAT_EXEMPT(component);

    proto::BindGeneration gen{};
    std::array<proto::DipChoice, cores::mra::kMaxDipRows> current{};
};

static_assert(std::is_trivially_copyable_v<DipChoices>);

using DipChoicesCell = xthread::Telemetry<DipChoices, SeatTag::RT>;

}
