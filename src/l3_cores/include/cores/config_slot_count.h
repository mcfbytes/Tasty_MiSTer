// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstdint>

#include "cores/types.h"

namespace mister::cores {

struct ConfigSlotCount {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr ConfigSlotCount truncating(I n) noexcept {
        return ConfigSlotCount{static_cast<std::uint8_t>(n)};
    }

    template <class F>
    constexpr void for_each_slot(F&& f) const {
        for (std::uint8_t i = 0; i < v; ++i)
            f(ConfigSlot{i});
    }

    friend constexpr bool operator==(ConfigSlotCount, ConfigSlotCount) = default;
    friend constexpr auto operator<=>(ConfigSlotCount, ConfigSlotCount) = default;
};

}
