// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "cores/config_codec.h"
#include "cores/config_slot_count.h"
#include "cores/types.h"
#include "infra/fixed_str.h"
#include "infra/index_set.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

using ConfigSlotMask = infra::
    IndexSet<cores::ConfigSlot, std::numeric_limits<std::uint16_t>::digits, std::uint16_t>;

struct ConfigSlotTable {
    TASTY_SEAT_EXEMPT(const_shared);
    cores::ConfigSlotCount count{};
    ConfigSlotMask exists{};
    infra::IndexedArray<
        FixedStr<cores::kConfigPreviewChars, StrFit::Clip>,
        cores::kMaxConfigSlots,
        cores::ConfigSlot>
        preview{};
};

static_assert(std::is_trivially_copyable_v<ConfigSlotTable>);
static_assert(cores::kMaxConfigSlots <= 16, "`exists` is a 16-bit mask");

using ConfigSlotCell = xthread::Telemetry<ConfigSlotTable, SeatTag::Main>;

}
