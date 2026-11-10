// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "app/types.h"
#include "infra/fixed_str.h"

namespace mister::app {

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

[[nodiscard]] constexpr HdCaption hd_count_text(RowCount n, std::string_view word) noexcept {
    HdCaption out{};
    (void)out.assign(n.text().view());
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
    RowIndex first,
    RowCount shown,
    RowCount total
) noexcept {
    const RowCount above = first.before();
    const RowCount below = saturating_sub(total, above + shown);
    return HdScrollCounts{
        hd_count_text(above, "above"),
        hd_count_text(below, "below"),
        hd_count_text(above, {}),
        hd_count_text(below, {})
    };
}

}
