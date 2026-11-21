// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>

#include "proto/spinner_delta.h"

namespace mister::proto {

struct SpinnerTravel {
    std::int32_t v = 0;

    [[nodiscard]] static constexpr SpinnerTravel of(SpinnerDelta step) noexcept {
        return SpinnerTravel{step.v};
    }

    [[nodiscard]] constexpr SpinnerTravel plus(SpinnerTravel more) const noexcept {
        std::int32_t sum = 0;
        if (!__builtin_add_overflow(v, more.v, &sum))
            return SpinnerTravel{sum};
        return SpinnerTravel{
            more.v < 0 ? std::numeric_limits<std::int32_t>::min()
                       : std::numeric_limits<std::int32_t>::max()
        };
    }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr SpinnerDelta delta() const noexcept { return SpinnerDelta::clamped(v); }

    [[nodiscard]] constexpr SpinnerTravel rest() const noexcept {
        const SpinnerTravel sent = of(delta());
        return SpinnerTravel{v - sent.v};
    }

    [[nodiscard]] constexpr bool overflows() const noexcept { return !rest().none(); }

    friend constexpr bool operator==(SpinnerTravel, SpinnerTravel) = default;
};

static_assert(SpinnerTravel{3}.plus(SpinnerTravel{-5}) == SpinnerTravel{-2}, "a signed sum");
static_assert(
    SpinnerTravel{300}.overflows() && !SpinnerTravel{127}.overflows(),
    "one word carries -128..127"
);
static_assert(
    SpinnerTravel{300}.rest() == SpinnerTravel{173}
        && SpinnerTravel{-300}.rest() == SpinnerTravel{-172},
    "the rest is what the cut left"
);
static_assert(
    SpinnerTravel{std::numeric_limits<std::int32_t>::min()}.rest()
        == SpinnerTravel{std::numeric_limits<std::int32_t>::min() + 128},
    "the rest of a held counter does not overflow"
);

}
