// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <limits>

#include "infra/json_out.h"

namespace mister::app {

struct StepCount {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr StepCount next() const noexcept {
        return StepCount{static_cast<std::uint8_t>(v + 1)};
    }

    [[nodiscard]] constexpr bool at_max() const noexcept {
        return v == std::numeric_limits<std::uint8_t>::max();
    }

    [[nodiscard]] constexpr bool reached(StepCount limit) const noexcept { return v >= limit.v; }

    [[nodiscard]] constexpr std::chrono::nanoseconds doublings_of(
        std::chrono::nanoseconds wait
    ) const noexcept {
        return std::chrono::nanoseconds{wait.count() << v};
    }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, StepCount c) noexcept {
        o.field(key, c.v);
    }

    friend constexpr bool operator==(StepCount, StepCount) = default;
    friend constexpr auto operator<=>(StepCount, StepCount) = default;
};

}
