// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/hd_window.h"
#include "app/page_description.h"
#include "ui/page_row.h"
#include "ui/types.h"

namespace mister::ui {

struct PageWalk {
    app::PageDescription& page;
    std::span<PageRow> rows;
    std::uint8_t count = 0;
    OsdRenderer::Arrow title_arrow = OsdRenderer::kArrowNone;

    [[nodiscard]] app::HdWindow window(RowIndex selected, RowCount total) noexcept;

    [[nodiscard]] RowCount copy_hd_rows() noexcept;
};

static_assert(sizeof(PageWalk) + sizeof(app::PageDescription) <= 16u * 1024u);

}
