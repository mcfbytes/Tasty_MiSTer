// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/page_walk.h"

#include <cstddef>
#include <cstdint>

#include "app/hd_window.h"
#include "ui/page_row.h"

namespace mister::ui {

app::HdWindow PageWalk::window(RowIndex selected, RowCount total) noexcept {
    const app::HdWindow win = app::hd_window(selected, total, page.first);
    page.first = win.first;
    page.total = total;
    return win;
}

RowCount PageWalk::copy_hd_rows() noexcept {
    std::uint8_t n = 0;
    for (std::uint8_t i = 0; i < count; ++i) {
        const PageRow& r = rows[i];
        if (!r.has_hd || r.legend)
            continue;
        if (static_cast<std::size_t>(n) >= app::PageDescription::kMaxRows)
            break;
        page.rows[n] = r.hd;
        ++n;
    }
    const RowCount copied = RowCount::of(n);
    page.row_count = copied;
    return copied;
}

}
