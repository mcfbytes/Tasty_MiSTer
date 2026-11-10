// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <utility>

namespace mister::app {

inline constexpr std::uint16_t kRecEveryMax = 600;

struct RecEvery {

    template <std::integral I>
    [[nodiscard]] static constexpr std::optional<RecEvery> of(I n) noexcept {
        if (n < I{1} || std::cmp_greater(n, kRecEveryMax))
            return std::nullopt;
        return RecEvery{static_cast<std::uint16_t>(n)};
    }

    [[nodiscard]] static constexpr RecEvery one() noexcept { return RecEvery{1}; }

    template <std::integral T>
    [[nodiscard]] constexpr T count() const noexcept {
        return static_cast<T>(v_);
    }

    friend constexpr bool operator==(RecEvery, RecEvery) = default;
    constexpr RecEvery() noexcept = default;

private:
    constexpr explicit RecEvery(std::uint16_t n) noexcept : v_(n) {}

    std::uint16_t v_ = 1;
};

}
