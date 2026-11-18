// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/block_poll_route.h"
#include "app/lw_pads.h"
#include "app/osd_grant.h"
#include "app/sys_grant.h"
#include "proto/types.h"

namespace mister::app {

struct SessionRoutes {
    std::optional<LwPads> pads{};
    OsdPageRoute osd = OsdPageRoute::Link;
    StatusRoute status = StatusRoute::Link;

    BlockPollRoute block_poll = BlockPollRoute::Link;

    proto::BlockSlotMask lw_slots{};

    [[nodiscard]] constexpr bool coherent() const noexcept {
        return block_poll != BlockPollRoute::Retired || !lw_slots.empty();
    }
};

}
