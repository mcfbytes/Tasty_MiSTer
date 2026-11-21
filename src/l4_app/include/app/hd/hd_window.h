// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

#include "app/hd/page_description.h"
#include "app/types.h"
#include "infra/run.h"

namespace mister::app::hd {

using HdWindow = infra::affine::Run<RowIndex>;

[[nodiscard]] constexpr HdWindow hd_window(
    RowIndex selected,
    RowCount total,
    RowIndex prev_first,
    RowCount height = RowCount::truncating(PageDescription::kMaxRows)
) noexcept {
    if (total.empty() || height.empty())
        return HdWindow{};
    const RowIndex end = row_after(total);
    const RowIndex sel = selected < end ? selected : end - RowCount{1};
    const RowCount count = std::min(total, height);
    RowIndex first = std::min(prev_first, end - count);
    if (sel < first) {
        first = sel;
    } else if (sel >= first + count) {
        first = sel - count + RowCount{1};
    }
    return HdWindow{first, count};
}

}
