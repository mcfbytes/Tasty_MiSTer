// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/link/block_poll_route.h"
#include "proto/block_roles.h"
#include "proto/link/block_slots.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/sector_floor.h"
#include "proto/session_live.h"

namespace mister::proto::link {

class SpiBlockDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::BlockRequest>;

    static constexpr unsigned kDrainBudget = 4;

    struct Counters {

        struct Retirements {
            std::uint32_t v = 0;

            constexpr Retirements& operator++() noexcept {
                ++v;
                return *this;
            }
        };

        std::uint32_t passes = 0;
        std::uint32_t served = 0;
        std::uint32_t errors = 0;
        std::uint32_t deferred = 0;
        std::uint32_t sectors = 0;
        Retirements idle_retirements{};
        std::uint16_t err_code = 0;
    };

    SpiBlockDecoder(ILinkRouter& router, BlockSlots& slots, const ISessionLive& live) noexcept
        : slots_(slots),
          live_(live),
          out_(router) {}

    void service(IBlockRoles& roles);

    [[nodiscard]] bool active() const noexcept {
        return live_.session_live() && life_ == PollLife::Polling;
    }

    void bind_route(BlockPollRoute route) noexcept {
        route_ = route;
        life_ = PollLife::Polling;
    }

    void bind_budget(unsigned budget) noexcept { budget_ = budget; }

    [[nodiscard]] const Counters& counters() const noexcept { return counters_; }

    void bind_sector_floor(SectorFloor floor) noexcept { floor_ = floor; }

    [[nodiscard]] std::uint32_t osd_claims() const noexcept { return counters_.sectors; }

private:
    enum class PollLife : std::uint8_t { Polling, Retired };

    BlockSlots& slots_;
    const ISessionLive& live_;
    Port out_;
    unsigned budget_ = kDrainBudget;
    SectorFloor floor_{};
    BlockPollRoute route_ = BlockPollRoute::Spi;
    PollLife life_ = PollLife::Polling;
    Counters counters_{};
};

}
