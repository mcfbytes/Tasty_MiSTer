// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdint>

#include "app/ratio.h"

namespace mister::app::hd {

class FloorRatioWalk {

    struct Whole {
        std::uint32_t v = 0;

        constexpr void add(Whole other) noexcept { v += other.v; }

        constexpr void bump() noexcept { ++v; }

        template <std::integral R>
        [[nodiscard]] constexpr R as() const noexcept {
            return static_cast<R>(v);
        }
    };

    struct Rem {
        std::uint32_t v = 0;

        constexpr void add(Rem other) noexcept { v += other.v; }

        [[nodiscard]] constexpr bool reaches(Rem den) const noexcept { return v >= den.v; }

        constexpr void wrap(Rem den) noexcept { v -= den.v; }
    };

public:
    [[nodiscard]] static constexpr FloorRatioWalk of(Ratio ratio) noexcept {
        constexpr std::uint32_t kOne = 1;
        const auto whole = ratio.applied_to<std::uint32_t, std::uint32_t>(kOne);
        const auto den = ratio.den.scaled<std::uint32_t, std::uint32_t>(kOne);
        const auto num = ratio.num.scaled<std::uint32_t, std::uint32_t>(kOne);
        const auto part = num - ratio.den.scaled<std::uint32_t, std::uint32_t>(whole);
        return FloorRatioWalk{Whole{whole}, Rem{part}, Rem{den}};
    }

    template <std::integral R>
    [[nodiscard]] constexpr R quotient() const noexcept {
        return quotient_.as<R>();
    }

    constexpr void step() noexcept {
        quotient_.add(whole_);
        rem_.add(part_);
        if (rem_.reaches(den_)) {
            rem_.wrap(den_);
            quotient_.bump();
        }
    }

private:
    constexpr FloorRatioWalk(Whole whole, Rem part, Rem den) noexcept
        : whole_(whole),
          part_(part),
          den_(den) {}

    Whole whole_;
    Rem part_;
    Rem den_;
    Whole quotient_{};
    Rem rem_{};
};

}
