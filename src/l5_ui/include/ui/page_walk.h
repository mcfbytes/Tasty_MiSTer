// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/hd/hd_window.h"
#include "app/hd/page_description.h"
#include "ui/page_row.h"
#include "ui/types.h"

namespace mister::ui {

struct PageWalk {
    app::hd::PageDescription& page;
    std::span<PageRow> rows;
    std::uint8_t count = 0;
    OsdRenderer::Arrow title_arrow = OsdRenderer::kArrowNone;

    [[nodiscard]] app::hd::HdWindow window(RowIndex selected, RowCount total) noexcept;

    [[nodiscard]] RowCount copy_hd_rows() noexcept;
};

static_assert(sizeof(PageWalk) + sizeof(app::hd::PageDescription) <= 16u * 1024u);

}
