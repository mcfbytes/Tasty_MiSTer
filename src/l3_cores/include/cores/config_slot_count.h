// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstdint>

namespace mister::cores {

struct ConfigSlotCount {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr ConfigSlotCount of(I n) noexcept {
        return ConfigSlotCount{static_cast<std::uint8_t>(n)};
    }

    friend constexpr bool operator==(ConfigSlotCount, ConfigSlotCount) = default;
    friend constexpr auto operator<=>(ConfigSlotCount, ConfigSlotCount) = default;
};

}
