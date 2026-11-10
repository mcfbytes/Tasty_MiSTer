// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

struct LoadWindowCounts {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t windows = 0;
    std::uint32_t refusals = 0;
    std::optional<Errc> refusal{};
    std::uint32_t cancelled = 0;
};

}
