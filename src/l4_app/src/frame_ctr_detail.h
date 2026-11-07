// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

#include "app/types.h"
#include "hal/types.h"

namespace mister::app {

inline constexpr CoreFrameGap kOneFrame{1};

inline constexpr CoreFrameGap kCtrReach = CoreFrameGap::of_lead(
    hal::HeaderCtr::Lag{hal::HeaderCtr::kMask}
);

inline constexpr CoreFrameGap kCtrTurn = kCtrReach + kOneFrame;

inline constexpr CoreFrameGap kCtrHalfTurn = kCtrTurn.halved();

[[nodiscard]] constexpr CoreFrameGap ctr_distance(hal::HeaderCtr from, hal::HeaderCtr to) noexcept {
    return CoreFrameGap::of_lead(to.lead_over(from));
}

[[nodiscard]] constexpr CoreFrameGap unwrapped_near(CoreFrameGap lead, CoreFrameGap near) noexcept {
    return lead + std::max(CoreFrameGap{}, (near - lead + kCtrHalfTurn).truncated_to(kCtrTurn));
}

}
