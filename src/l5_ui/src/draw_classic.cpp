// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_classic.h"

#include <cstdint>

#include "ui/osd_renderer.h"
#include "ui/page.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"

namespace mister::ui {

PageRow::Classic classic_row(
    proto::OsdRow row,
    std::string_view text,
    ClassicMarks marks
) noexcept {
    PageRow::Classic c;
    (void)c.text.assign(text);
    c.row = row;
    c.leftchar = marks.leftchar;
    c.arrow = marks.arrow;
    c.inverted = marks.inverted;
    c.stippled = marks.stippled;
    return c;
}

void Page::draw_classic(OsdRenderer& out, const PageWalk& w) const {
    TASTY_SEAT_BODY(Page);
    out.set_title(w.page.title.view(), w.title_arrow);
    for (std::uint8_t i = 0; i < w.count; ++i) {
        const PageRow& r = w.rows[i];
        if (!r.classic || !out.in_height(r.classic->row))
            continue;
        out.set_arrow(r.classic->arrow.value_or(w.title_arrow));
        out.text(
            r.classic->row,
            r.classic->text.view(),
            r.classic->inverted,
            r.classic->stippled,
            r.classic->leftchar
        );
    }
}

}
