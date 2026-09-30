// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

struct FallbackCounts {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t start_timeouts = 0;
    std::uint32_t front_end_asked = 0;
    std::uint32_t front_end_failed = 0;
};

}  // namespace mister::app
