// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>
#include <type_traits>

#include "app/hd_osd_status.h"
#include "app/hd_status_slots.h"

namespace mister::app {

struct HdBadgeView {
    HdOsdStatus::Badge badge = HdOsdStatus::Badge::Unknown;
    HdStatusSlots slots{};
};

[[nodiscard]] constexpr std::string_view hd_badge_text(HdOsdStatus::Badge badge) noexcept {
    switch (badge) {
        case HdOsdStatus::Badge::Running:
            return "Game running";
        case HdOsdStatus::Badge::Paused:
            return "Paused";
        case HdOsdStatus::Badge::Unknown:
            break;
    }
    return {};
}

static_assert(std::is_trivially_copyable_v<HdBadgeView>);
static_assert(std::is_nothrow_default_constructible_v<HdBadgeView>);

}
