// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/decimal_text.h"
#include "infra/seat.h"

namespace mister::proto {

struct FrameDrops {
    TASTY_SEAT_EXEMPT(component);

    std::uint32_t v = 0;

    constexpr void add() noexcept { ++v; }

    [[nodiscard]] constexpr FrameDrops since(FrameDrops earlier) const noexcept {
        return FrameDrops{v - earlier.v};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(FrameDrops, FrameDrops) = default;
};

}
