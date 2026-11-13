// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "svc/types.h"

namespace mister::app {

struct OsdRowTally {
    TASTY_SEAT_RESIDENT(RT);
    std::uint32_t rows_flushed = 0;
    std::uint32_t flush_failures = 0;
    std::uint32_t commits_refused = 0;
    std::uint32_t sample_refusals = 0;
    std::uint32_t starved = 0;
    std::uint32_t backlog_hw = 0;

    std::uint32_t page_rows_sent = 0;
    std::uint32_t status_rows_sent = 0;

    svc::Tally menu_ctls_sent{};

    svc::Tally rotations_sent{};
    svc::Tally rotation_failures{};
};

}
