// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "app/hd_osd_status.h"

namespace mister::app {

struct HdBadgeView {
    HdOsdStatus::Badge badge = HdOsdStatus::Badge::Unknown;
    std::uint8_t fps = 0;
};

static_assert(static_cast<std::uint8_t>(HdOsdStatus::Badge::Unknown) == 0);
static_assert(static_cast<std::uint8_t>(HdOsdStatus::Badge::Running) == 1);
static_assert(static_cast<std::uint8_t>(HdOsdStatus::Badge::Paused) == 2);
static_assert(std::is_trivially_copyable_v<HdBadgeView>);
static_assert(std::is_nothrow_default_constructible_v<HdBadgeView>);

}  // namespace mister::app
