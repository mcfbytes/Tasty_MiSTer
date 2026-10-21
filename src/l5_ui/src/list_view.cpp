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
    if (count.v == 0 || height.v == 0)
        return v;

    std::uint32_t sel = selected.v;
    if (sel >= count.v)
        sel = count.v - 1u;
    v.selected = RowIndex{sel};

    const std::uint32_t max_first = (count.v > height.v) ? (count.v - height.v) : 0u;

    std::uint32_t first = 0;
    if (p.lookahead) {

        const std::uint32_t half = height.v / 2u;
        first = (sel > half) ? (sel - half) : 0u;
    } else {

        first = (sel >= height.v) ? (sel - height.v + 1u) : 0u;
    }

    if (first > max_first)
        first = max_first;
    v.first = RowIndex{first};
    return v;
}

}
