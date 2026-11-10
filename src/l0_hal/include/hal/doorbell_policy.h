// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/phys_region.h"
#include "hal/types.h"
#include "os/types.h"

namespace mister::hal {

struct DoorbellPolicy {
    std::uint32_t pool = 0;
    LwOffset cause_base{0};
    std::uint32_t cause_span = 0;

    [[nodiscard]] constexpr bool line_in_pool(os::UioLine line) const noexcept {
        return os::to_ordinal(line) < pool;
    }

    [[nodiscard]] constexpr bool cause_in_aperture(LwOffset off, std::size_t len) const noexcept {
        if (cause_span == 0 || len == 0 || off < cause_base)
            return false;
        return span_fits<std::size_t>(off.distance_to(cause_base), len, cause_span);
    }
};

}
