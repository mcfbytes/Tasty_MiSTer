// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/log_clock.h"
#include "infra/seat.h"

namespace mister::app {

struct LatStat {
    TASTY_SEAT_EXEMPT(component);
    StatusMicros last{};
    StatusMicros max{};

    constexpr void note(StatusMicros span) noexcept {
        last = span;
        if (span > max)
            max = span;
    }

    friend constexpr bool operator==(const LatStat&, const LatStat&) = default;
};

}
