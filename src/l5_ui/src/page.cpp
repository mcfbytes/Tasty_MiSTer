// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/page.h"

#include <cstdint>

#include "ui/page_walk.h"

namespace mister::ui {

std::uint8_t copy_hd_rows(PageWalk& w) noexcept {
    std::uint8_t n = 0;
    for (std::uint8_t i = 0; i < w.count; ++i) {
        const PageRow& r = w.rows[i];
        if (!r.has_hd || r.legend)
            continue;
        if (static_cast<std::size_t>(n) >= app::PageDescription::kMaxRows)
            break;
        w.page.rows[n] = r.hd;
        ++n;
    }
    w.page.row_count = app::RowCount::of(n);
    return n;
}

}
