// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstdint>

#include "infra/json_out.h"

namespace mister::app {

struct Percent {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr Percent of(I p) noexcept {
        constexpr std::uint8_t kFull = 100;
        return Percent{static_cast<std::uint8_t>(p > kFull ? kFull : p)};
    }

    template <std::integral I>
    [[nodiscard]] static constexpr Percent measured(I p) noexcept {
        constexpr std::uint32_t kMax = 0xFF;
        const std::uint32_t n = static_cast<std::uint32_t>(p);
        return Percent{static_cast<std::uint8_t>(n > kMax ? kMax : n)};
    }

    template <std::integral T = std::uint8_t>
    [[nodiscard]] constexpr T count() const noexcept {
        return static_cast<T>(v);
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    template <std::integral T>
    [[nodiscard]] constexpr T share_of(T whole) const noexcept {
        constexpr T kFull = 100;
        return static_cast<T>(static_cast<std::int64_t>(whole) * v / kFull);
    }

    template <std::integral T>
    [[nodiscard]] constexpr T share_of_rounded(T whole) const noexcept {
        constexpr T kFull = 100, kRound = kFull / 2;
        return static_cast<T>((static_cast<std::int64_t>(whole) * v + kRound) / kFull);
    }

    template <std::integral T = std::uint16_t>
    [[nodiscard]] constexpr T compose_q8(Percent under) const noexcept {
        constexpr std::uint32_t kFull = 100;
        constexpr std::uint32_t kQ8 = 256;
        const std::uint32_t open = v > kFull ? kFull : v;
        const std::uint32_t under_v = under.v > kFull ? kFull : under.v;
        return static_cast<T>(open * under_v * kQ8 / (kFull * kFull));
    }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, Percent p) noexcept {
        o.field(key, p.v);
    }

    friend constexpr bool operator==(Percent, Percent) = default;
    friend constexpr auto operator<=>(Percent, Percent) = default;
};

}
