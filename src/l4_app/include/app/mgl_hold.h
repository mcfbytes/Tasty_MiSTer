// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>

namespace mister::app {

struct ResetHold {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr ResetHold of(I s) noexcept {
        constexpr std::uint32_t kMax = std::numeric_limits<std::uint8_t>::max();
        const std::uint32_t n = static_cast<std::uint32_t>(s);
        return ResetHold{static_cast<std::uint8_t>(n > kMax ? kMax : n)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::chrono::seconds as_seconds() const noexcept {
        return std::chrono::seconds{v};
    }

    friend constexpr bool operator==(ResetHold, ResetHold) = default;
    friend constexpr auto operator<=>(ResetHold, ResetHold) = default;
};

}
