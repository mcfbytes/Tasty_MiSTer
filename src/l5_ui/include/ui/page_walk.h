// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/page_description.h"
#include "ui/page_row.h"

namespace mister::ui {

struct PageWalk {
    app::PageDescription& page;
    std::span<PageRow> rows;
    std::uint8_t count = 0;
    OsdRenderer::Arrow title_arrow = OsdRenderer::kArrowNone;
};

[[nodiscard]] std::uint8_t copy_hd_rows(PageWalk& w) noexcept;

static_assert(sizeof(PageWalk) + sizeof(app::PageDescription) <= 16u * 1024u);

}
