// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::proto {

struct InPlaceOpens {
    TASTY_SEAT_EXEMPT(component);
    std::uint32_t v = 0;

    constexpr void opened() noexcept { ++v; }

    [[nodiscard]] constexpr bool closed() noexcept {
        if (v == 0)
            return false;
        --v;
        return true;
    }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    friend constexpr bool operator==(InPlaceOpens, InPlaceOpens) = default;
};

}
