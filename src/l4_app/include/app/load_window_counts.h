// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

struct LoadWindowCounts {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t windows = 0;
    std::uint32_t refusals = 0;
    std::uint16_t refusal_code = 0;
    std::uint16_t pad_ = 0;
    std::uint32_t cancelled = 0;
};

}  // namespace mister::app
