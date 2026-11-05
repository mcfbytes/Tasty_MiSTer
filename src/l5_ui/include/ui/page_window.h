// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/hd_window.h"
#include "app/page_description.h"
#include "ui/types.h"

namespace mister::ui {

app::HdWindow window_page(app::PageDescription& page, RowIndex selected, RowCount total) noexcept;

}
