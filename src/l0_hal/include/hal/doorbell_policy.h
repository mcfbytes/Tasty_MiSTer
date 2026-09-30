// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/types.h"

namespace mister::hal {

struct DoorbellPolicy {
    std::uint32_t pool = 0;
    LwOffset cause_base{0};
    std::uint32_t cause_span = 0;

    [[nodiscard]] constexpr bool line_in_pool(std::uint32_t line) const noexcept {
        return line < pool;
    }

    [[nodiscard]] constexpr bool cause_in_aperture(LwOffset off, std::size_t len) const noexcept {
        if (cause_span == 0 || len == 0 || len > cause_span) return false;
        if (off.v < cause_base.v) return false;
        return static_cast<std::size_t>(off.v - cause_base.v) <= cause_span - len;
    }
};

}  // namespace mister::hal
