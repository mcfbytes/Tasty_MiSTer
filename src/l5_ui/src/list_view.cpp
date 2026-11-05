// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/list_view.h"

namespace mister::ui {

Viewport Viewport::centre(
    RowIndex selected,
    RowCount count,
    RowCount height,
    ViewportPolicy p
) noexcept {
    Viewport v{};
    if (count.empty() || height.empty())
        return v;

    const RowIndex sel = selected >= row_after(count) ? last_row(count) : selected;
    v.selected = sel;

    const RowCount max_first = saturating_sub(count, height);

    RowIndex first{};
    if (p.lookahead) {

        const RowCount half = height.half();
        first = (sel.before() > half) ? sel - half : RowIndex{};
    } else {

        first = (sel.before() >= height) ? sel - height + kOneRow : RowIndex{};
    }

    if (first.before() > max_first)
        first = row_after(max_first);
    v.first = first;
    return v;
}

}
