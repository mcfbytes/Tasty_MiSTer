// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "hal/types.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/tally.h"
#include "infra/xthread/telemetry.h"
#include "svc/types.h"

namespace mister::app {

struct RumbleLevels {
    infra::IndexedArray<hal::LinkWord, svc::kMaxPlayers, svc::PlayerIndex> levels{};
    infra::Tally count{};
};

static_assert(std::is_trivially_copyable_v<RumbleLevels>);
using RumbleLevelsCell = xthread::Telemetry<RumbleLevels, SeatTag::Io>;

}
