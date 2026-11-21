// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/carrier.h"
#include "proto/link/block_poll_route.h"
#include "proto/types.h"

namespace mister::app {

struct SessionRoutes {
    Carrier pads = Carrier::Link;
    Carrier osd = Carrier::Link;
    Carrier status = Carrier::Link;

    proto::BlockSlotMask lw_slots{};

    proto::BlockSlotMask declared_slots{};

    [[nodiscard]] constexpr proto::link::BlockPollRoute block_poll() const noexcept {
        return !lw_slots.empty() && declared_slots.minus(lw_slots).empty()
            ? proto::link::BlockPollRoute::UntilIdle
            : proto::link::BlockPollRoute::Link;
    }
};

}
