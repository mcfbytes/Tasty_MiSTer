// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

struct SessionTally {
    TASTY_SEAT_RESIDENT(RT);
    std::uint32_t mailbox_write_drops = 0;
    std::uint32_t link_ops_encoded = 0;
    std::uint32_t link_op_drops = 0;
    std::uint32_t link_op_misrouted = 0;

    std::uint32_t stale_owner_steps = 0;

    std::uint32_t start_refused_drops = 0;

    std::uint32_t core_made_drops = 0;

    std::uint32_t resets_pulsed = 0;

    std::uint32_t load_facts_refusals = 0;

    std::uint32_t osd_close_hooks = 0;

    Error last_error{};
};

}
