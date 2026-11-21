// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/page_label.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "infra/tally.h"

namespace mister::app {

struct UiPageRecord {
    std::uint32_t depth = 0;
    PageLabel top_id{};
    std::uint32_t vis_publishes = 0;

    std::uint32_t cell_refusals = 0;

    infra::Tally page_refusals{};

    std::uint32_t button_levels = 0;
    std::uint32_t button_edges = 0;
    std::uint32_t button_longs = 0;
    std::uint32_t button_classic = 0;
    std::uint32_t button_injects = 0;
    std::uint32_t button_core_resets = 0;

    infra::Tally button_actions{};
    std::uint32_t last_hold_ms = 0;

    std::uint32_t recent_build_us = 0;
    std::uint32_t recent_update_us_max = 0;

    std::uint32_t lane_refusals = 0;
    std::uint32_t lane_failures = 0;
    std::uint32_t store_errors = 0;
};

using UiPageCell = xthread::Telemetry<UiPageRecord, SeatTag::Ui>;

}
