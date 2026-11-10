// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstdint>

namespace mister::app {

struct Ratio {

    struct Term {
        std::uint64_t v = 1;

        template <std::integral I>
        [[nodiscard]] static constexpr Term of(I n) noexcept {
            return Term{static_cast<std::uint64_t>(n)};
        }

        template <std::integral I, std::integral R = std::uint64_t>
        [[nodiscard]] constexpr R scaled(I x) const noexcept {
            return static_cast<R>(static_cast<R>(x) * v);
        }

        template <std::integral I, std::integral R = std::uint64_t>
        [[nodiscard]] constexpr R divided(I x) const noexcept {
            return static_cast<R>(static_cast<R>(x) / v);
        }

        friend constexpr bool operator==(Term, Term) = default;
        friend constexpr auto operator<=>(Term, Term) = default;
    };

    Term num{};
    Term den{};

    template <std::integral I, std::integral R = std::uint64_t>
    [[nodiscard]] constexpr R applied_to(I value) const noexcept {
        return den.divided<R, R>(num.scaled<I, R>(value));
    }

    friend constexpr bool operator==(Ratio, Ratio) = default;
};

}
