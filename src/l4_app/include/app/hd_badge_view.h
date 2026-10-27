// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "app/hd_osd_status.h"
#include "app/hd_status_slots.h"
#include "infra/fixed_str.h"

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

using HdCaption = FixedStr<48, StrFit::Clip>;

inline constexpr std::string_view kHdShelfCaption = "Recent games";
inline constexpr std::string_view kHdStripCaption = "Quick actions";

[[nodiscard]] constexpr HdCaption hd_now_playing(std::string_view core) noexcept {
    HdCaption out{};
    (void)out.assign("NOW PLAYING");
    if (!core.empty()) {
        (void)out.append(" \u00B7 ");
        (void)out.append(core);
    }
    return out;
}

[[nodiscard]] constexpr HdCaption hd_count_text(std::uint32_t n, std::string_view word) noexcept {
    char digits[10]{};
    std::size_t len = 0;
    do {
        digits[len++] = static_cast<char>('0' + n % 10u);
        n /= 10u;
    } while (n != 0u && len < sizeof(digits));
    HdCaption out{};
    for (std::size_t i = len; i > 0; --i)
        (void)out.append(std::string_view{&digits[i - 1], 1});
    if (word.empty())
        return out;
    (void)out.append(" ");
    (void)out.append(word);
    return out;
}

struct HdScrollCounts {
    HdCaption above{};
    HdCaption below{};
    HdCaption above_n{};
    HdCaption below_n{};
};

[[nodiscard]] constexpr HdScrollCounts hd_scroll_counts(
    std::uint32_t first,
    std::uint32_t shown,
    std::uint32_t total
) noexcept {
    const std::uint32_t seen = first + shown;
    const std::uint32_t rest = total > seen ? total - seen : 0u;
    return HdScrollCounts{
        hd_count_text(first, "above"),
        hd_count_text(rest, "below"),
        hd_count_text(first, {}),
        hd_count_text(rest, {})
    };
}

static_assert(std::is_trivially_copyable_v<HdBadgeView>);
static_assert(std::is_nothrow_default_constructible_v<HdBadgeView>);

}
