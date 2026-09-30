// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "cores/cheat_geometry.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct CheatBlob {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint16_t len = 0;
    std::uint32_t unit = 16;
    std::uint8_t bytes[cores::kCheatTableBytes] = {};
};
static_assert(std::is_trivially_copyable_v<CheatBlob>);

using CheatBlobCell = xthread::Telemetry<CheatBlob, SeatTag::Ui>;

}  // namespace mister::app
