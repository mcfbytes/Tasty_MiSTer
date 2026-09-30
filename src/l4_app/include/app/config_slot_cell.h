// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "cores/config_slots.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct ConfigSlotTable {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint8_t count = 0;
    std::uint16_t exists = 0;
    FixedStr<cores::kConfigPreviewChars, StrFit::Clip> preview[cores::kMaxConfigSlots] = {};
};
static_assert(std::is_trivially_copyable_v<ConfigSlotTable>);
static_assert(cores::kMaxConfigSlots <= 16, "`exists` is a 16-bit mask");

using ConfigSlotCell = xthread::Telemetry<ConfigSlotTable, SeatTag::RT>;

}  // namespace mister::app
