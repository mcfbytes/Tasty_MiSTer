// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/block_slots.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/sector_floor.h"
#include "proto/session_live.h"

namespace mister::proto {

class SpiBlockDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::BlockRequest>;

    static constexpr unsigned kDrainBudget = 4;

    struct Counters {
        std::uint32_t passes = 0;
        std::uint32_t served = 0;
        std::uint32_t errors = 0;
        std::uint32_t deferred = 0;
        std::uint32_t sectors = 0;
        std::uint16_t err_code = 0;
    };

    SpiBlockDecoder(hal::ISpiTransport& link, ILinkRouter& router, BlockSlots& slots,
                    const ISessionLive& live) noexcept
        : link_(&link), slots_(&slots), live_(&live), out_(router) {}

    void service();
    [[nodiscard]] bool active() const noexcept { return live_->session_live(); }

    void bind_budget(unsigned budget) noexcept { budget_ = budget; }
    [[nodiscard]] const Counters& counters() const noexcept { return counters_; }

    void bind_sector_floor(SectorFloor floor) noexcept { floor_ = floor; }

    [[nodiscard]] std::uint32_t osd_claims() const noexcept { return counters_.sectors; }

private:
    hal::ISpiTransport* link_;
    BlockSlots* slots_;
    const ISessionLive* live_;
    Port out_;
    unsigned budget_ = kDrainBudget;
    SectorFloor floor_{};
    Counters counters_{};
};

}  // namespace mister::proto
