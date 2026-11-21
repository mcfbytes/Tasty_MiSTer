// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/page_walk.h"

#include <cstddef>
#include <cstdint>

#include "app/hd/hd_window.h"
#include "ui/page_row.h"

namespace mister::ui {

app::hd::HdWindow PageWalk::window(RowIndex selected, RowCount total) noexcept {
    const app::hd::HdWindow win = app::hd::hd_window(selected, total, page.first);
    page.first = win.start;
    page.total = total;
    return win;
}

RowCount PageWalk::copy_hd_rows() noexcept {
    std::uint8_t n = 0;
    for (std::uint8_t i = 0; i < count; ++i) {
        const PageRow& r = rows[i];
        if (!r.has_hd || r.legend)
            continue;
        if (static_cast<std::size_t>(n) >= app::hd::PageDescription::kMaxRows)
            break;
        page.rows[n] = r.hd;
        ++n;
    }
    const RowCount copied = RowCount::truncating(n);
    page.row_count = copied;
    return copied;
}

}
