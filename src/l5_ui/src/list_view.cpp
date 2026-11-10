// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/list_view.h"

namespace mister::ui {

Viewport Viewport::centre(RowIndex selected, ListWindow window, ViewportPolicy p) noexcept {
    Viewport v{};
    if (window.items.empty() || window.height.empty())
        return v;

    const RowIndex sel = selected >= row_after(window.items) ? last_row(window.items) : selected;
    v.selected = sel;

    const RowCount max_first = saturating_sub(window.items, window.height);

    RowIndex first{};
    if (p.lookahead) {

        const RowCount half = window.height.half();
        first = (sel.before() > half) ? sel - half : RowIndex{};
    } else {

        first = (sel.before() >= window.height) ? sel - window.height + kOneRow : RowIndex{};
    }

    if (first.before() > max_first)
        first = row_after(max_first);
    v.first = first;
    return v;
}

}
