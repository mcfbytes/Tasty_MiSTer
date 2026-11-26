// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/carrier.h"
#include "proto/link/block_poll_route.h"
#include "proto/types.h"

namespace mister::app {

struct SessionRoutes {
    Carrier pads = Carrier::Spi;
    Carrier osd = Carrier::Spi;
    Carrier status = Carrier::Spi;

    Carrier downloads = Carrier::Spi;
    Carrier frames = Carrier::Spi;
    Carrier events = Carrier::Spi;

    proto::BlockSlotMask off_link_slots{};

    proto::BlockSlotMask declared_slots{};

    [[nodiscard]] constexpr proto::link::BlockPollRoute block_poll() const noexcept {
        return !off_link_slots.empty() && declared_slots.minus(off_link_slots).empty()
            ? proto::link::BlockPollRoute::UntilIdle
            : proto::link::BlockPollRoute::Spi;
    }
};

}
