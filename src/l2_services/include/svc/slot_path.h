// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::svc {

inline constexpr std::size_t kSlotPathCap = 1024;

struct SlotPath {
    TASTY_SEAT_MEDIATOR(Any, Any);
    FixedStr<kSlotPathCap, StrFit::Reject> path{};
};

}  // namespace mister::svc
