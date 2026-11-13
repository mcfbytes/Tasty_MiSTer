// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "svc/disc_rules.h"
#include "svc/disc_mounter.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr std::size_t kDiscPathCap = infra::kPathMax;

inline constexpr std::size_t kDiscMountDepth = 2;

struct DiscMountSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    FixedStr<kDiscPathCap, StrFit::Reject> path{};

    infra::OptRef<const CuePolicy> policy{};

    infra::OptRef<const IDiscRules> rules{};
    Generation gen{};
    DiscOp op = DiscOp::Mount;
    bool mounted = false;
};

static_assert(std::is_trivially_copyable_v<DiscMountSlot>);

}
