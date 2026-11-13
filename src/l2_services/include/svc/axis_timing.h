// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct AxisTiming {
    std::uint32_t active = 0;
    std::uint32_t front_porch = 0;
    std::uint32_t sync = 0;
    std::uint32_t back_porch = 0;

    [[nodiscard]] constexpr std::uint32_t total() const noexcept {
        return active + front_porch + sync + back_porch;
    }

    friend constexpr bool operator==(AxisTiming, AxisTiming) = default;
};

}
