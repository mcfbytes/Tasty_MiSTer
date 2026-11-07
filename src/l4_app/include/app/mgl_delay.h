// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

#include "infra/seat.h"

namespace mister::app {

struct DelaySeconds {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr DelaySeconds of(std::uint32_t s) noexcept {
        constexpr std::uint32_t kMax = std::numeric_limits<std::uint16_t>::max();
        return DelaySeconds{static_cast<std::uint16_t>(s > kMax ? kMax : s)};
    }

    [[nodiscard]] constexpr std::chrono::seconds as_seconds() const noexcept {
        return std::chrono::seconds{v};
    }

    friend constexpr bool operator==(DelaySeconds, DelaySeconds) = default;
};

}
