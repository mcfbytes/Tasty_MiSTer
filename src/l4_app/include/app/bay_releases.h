// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "app/built_core.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "infra/telemetry.h"

namespace mister::app {

using BayGens = std::array<std::uint16_t, kCoreBaySlots>;

struct BayReleases {
    BayGens released_gen{};
};

static_assert(std::is_trivially_copyable_v<BayReleases>);

using BayReleasesCell = xthread::Telemetry<BayReleases, SeatTag::RT>;

}
