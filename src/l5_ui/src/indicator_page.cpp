// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/indicator_page.h"

#include <array>
#include <span>

#include "ui/osd_renderer.h"

namespace mister::ui {

static_assert(
    proto::kInfoMaxWidth == proto::OsdColCount::saturating(IndicatorCanvas::Line::kCapacity),
    "a canvas line is as wide as the widest info line"
);

PageFacts IndicatorPage::facts() const {
    TASTY_SEAT_BODY(IndicatorPage);
    PageFacts f{};
    f.has_title_strip = false;
    f.takes_input_focus = false;
    f.allows_popups = true;
    f.counts_as_menu_present = false;
    return f;
}

IndicatorBadge IndicatorPage::badge_() const noexcept {
    IndicatorBadge::Lines lines{};
    std::size_t n = 0;
    for (proto::OsdRow row{}; n < lines.size(); row += proto::kOneOsdRow) {
        const infra::OptRef<const IndicatorCanvas> line = board_.line(row);
        if (!line)
            break;
        lines[n++] = *line;
    }
    return IndicatorBadge{std::span<const IndicatorCanvas>{lines}.first(n)};
}

std::optional<proto::InfoWindow> IndicatorPage::info_window() const {
    TASTY_SEAT_BODY(IndicatorPage);
    const IndicatorBadge badge = badge_();
    if (badge.height() == proto::OsdRowCount{})
        return std::nullopt;
    return proto::InfoWindow{
        .x = proto::kInfoOriginX,
        .y = proto::kInfoWindowY,
        .width = badge.width(),
        .height = badge.height()
    };
}

void IndicatorPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(IndicatorPage);
    const IndicatorBadge badge = badge_();
    for (proto::OsdRow row{}; badge.height().covers(row); row += proto::kOneOsdRow) {
        out.columns(row, badge.row(row));
    }
}

std::unique_ptr<Page> make_indicator_page(const IndicatorBoard& board) {
    return std::make_unique<IndicatorPage>(board);
}

}
