// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct UiPageRecord {
    std::uint32_t depth = 0;
    std::uint32_t top_id = 0;
    std::uint32_t vis_publishes = 0;

    std::uint32_t cell_refusals = 0;

    std::uint32_t page_refusals = 0;

    std::uint32_t button_levels = 0;
    std::uint32_t button_edges = 0;
    std::uint32_t button_longs = 0;
    std::uint32_t button_classic = 0;
    std::uint32_t button_injects = 0;
    std::uint32_t button_core_resets = 0;
    std::uint32_t last_hold_ms = 0;
};

using UiPageCell = xthread::Telemetry<UiPageRecord, SeatTag::Ui>;

}
