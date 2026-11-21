// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>

#include "proto/stick_pos.h"

namespace mister::proto {

struct SpinnerDelta {
    std::int8_t v = 0;

    [[nodiscard]] static constexpr SpinnerDelta clamped(std::int32_t counts) noexcept {
        constexpr std::int32_t lo = std::numeric_limits<std::int8_t>::min();
        constexpr std::int32_t hi = std::numeric_limits<std::int8_t>::max();
        if (counts < lo)
            return SpinnerDelta{std::numeric_limits<std::int8_t>::min()};
        if (counts > hi)
            return SpinnerDelta{std::numeric_limits<std::int8_t>::max()};
        return SpinnerDelta{static_cast<std::int8_t>(counts)};
    }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr StickPos stick() const noexcept {
        return StickPos{static_cast<std::uint8_t>(v), 0};
    }

    friend constexpr bool operator==(SpinnerDelta, SpinnerDelta) = default;
};

static_assert(SpinnerDelta::clamped(-500) == SpinnerDelta{-128}, "the low stop of one word");
static_assert(SpinnerDelta::clamped(500) == SpinnerDelta{127}, "the high stop of one word");
static_assert(SpinnerDelta{-1}.stick() == StickPos{0xFF, 0}, "two's complement in X, Y zero");

}
