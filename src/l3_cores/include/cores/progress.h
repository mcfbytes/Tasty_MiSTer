// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace mister::cores {

struct Progress {
    std::uint16_t done = 0;
    std::uint16_t total = 0;

    [[nodiscard]] constexpr bool dismisses() const noexcept { return total == 0; }

    [[nodiscard]] constexpr std::uint32_t scaled(std::uint32_t steps) const noexcept {
        if (dismisses())
            return 0;

        const std::uint32_t landed = done * steps / total;
        return std::min(landed, steps);
    }

    friend constexpr bool operator==(Progress, Progress) = default;
};

}
