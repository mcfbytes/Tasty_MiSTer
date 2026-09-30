// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/disc_mounter.h"

namespace mister::svc {

inline constexpr std::size_t kDiscPathCap = 1024;

inline constexpr std::size_t kDiscMountDepth = 2;

struct DiscMountSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    FixedStr<kDiscPathCap, StrFit::Reject> path{};

    const CuePolicy* policy = nullptr;
    std::uint32_t gen = 0;
    DiscOp op = DiscOp::Mount;
    std::uint8_t mounted = 0;
    std::uint8_t done = 0;
};

static_assert(std::is_trivially_copyable_v<DiscMountSlot>);

}  // namespace mister::svc
