// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "cores/dip_row.h"
#include "cores/switch_table.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct DipTable {
    TASTY_SEAT_EXEMPT(const_shared);
    struct Row {
        char name[cores::mra::kDipTextChars + 1] = {};
        std::uint8_t choices = 0;
        std::uint8_t current = 0;
        char ids[cores::mra::kMaxDipIds][cores::mra::kDipTextChars + 1] = {};
    };
    std::uint8_t count = 0;
    Row rows[cores::mra::kMaxDipRows] = {};
};
static_assert(std::is_trivially_copyable_v<DipTable>);

using DipCell = xthread::Telemetry<DipTable, SeatTag::RT>;

}  // namespace mister::app
