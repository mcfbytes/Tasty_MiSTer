// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/hd_metrics.h"
#include "app/page_description.h"
#include "app/types.h"
#include "infra/affine.h"
#include "infra/indexed_array.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;

[[nodiscard]] constexpr bool choices_in_a_row(RowCount n) noexcept {
    constexpr RowCount kMaxChoicesInARow = RowCount::of(2);
    return n <= kMaxChoicesInARow;
}

[[nodiscard]] constexpr bool choices_in_a_row(std::size_t n) noexcept {
    return choices_in_a_row(RowCount::of(n));
}

struct HdPanelPlan {

    RowCount main_count{};
    RowCount main_seps{};
    RowIndex pinned_first{};
    RowCount pinned_count{};
    RowCount pinned_seps{};
    RowIndex choice_first{};
    RowCount choice_count{};
    bool choice_stacked = false;
    std::int32_t row_h = 0;
    std::int32_t sep_h = 0;
    std::int32_t main_y = 0;
    std::int32_t main_h = 0;
    std::int32_t pinned_y = 0;
    std::int32_t pinned_h = 0;
    std::int32_t choice_y = 0;
    std::int32_t choice_h = 0;
    bool cue = false;
    bool cue_up = false;
    bool cue_down = false;

    RowCount cue_at{};
    RowCount cue_total{};
    bool steps = false;
};

inline constexpr RowCount kOneRow = RowCount::of(1);

[[nodiscard]] constexpr HdPanelPlan plan_panel(
    const HdMetrics& m,
    const PageDescription& page
) noexcept {
    using Kind = PageDescription::RowKind;

    constexpr std::int32_t kHalvesPerRow = 2, kHalvesPerSeparator = 1, kFrameSides = 2;
    HdPanelPlan p{};
    const RowCount n = page.listed();
    const RowIndex end = RowIndex{} + n;
    const bool choice = page.shape == PageDescription::Shape::Choice;
    RowIndex body_end = end;
    if (choice) {
        body_end = RowIndex{};
        while (body_end < end && page.row(body_end).kind == Kind::Text)
            body_end += kOneRow;
        p.choice_first = body_end;
        p.choice_count = end - body_end;
        p.choice_stacked = !choices_in_a_row(p.choice_count);
    } else {
        const RowCount pinned = std::min(page.pinned, n);
        p.pinned_first = end - pinned;
        p.pinned_count = pinned;
        body_end = p.pinned_first;
        if (!pinned.empty()
            && body_end != RowIndex{}
            && page.row(body_end - kOneRow).kind == Kind::Separator)
            body_end -= kOneRow;
    }
    p.main_count = body_end.before();
    for (RowIndex i{}; i < end; i += kOneRow) {
        const bool sep = page.row(i).kind == Kind::Separator;
        if (sep && i < body_end)
            p.main_seps += kOneRow;
        if (sep && i >= p.pinned_first && !p.pinned_count.empty())
            p.pinned_seps += kOneRow;
        if (page.row(i).selected && page.row(i).steps)
            p.steps = true;
    }

    const std::int32_t frame = kFrameSides * (m.card_pad + m.stroke);
    const std::int32_t bottom = m.help_top - m.gap;
    std::int32_t fixed = 0;
    RowExtent halves = (p.main_count - p.main_seps).extent(RowPitch{kHalvesPerRow})
        + p.main_seps.extent(RowPitch{kHalvesPerSeparator});
    if (!p.main_count.empty())
        fixed += frame;
    if (!p.pinned_count.empty()) {
        fixed += frame + m.gap;
        halves = halves
            + (p.pinned_count - p.pinned_seps).extent(RowPitch{kHalvesPerRow})
            + p.pinned_seps.extent(RowPitch{kHalvesPerSeparator});
    }
    if (!p.choice_count.empty()) {
        const RowCount rows = p.choice_stacked ? p.choice_count : kOneRow;
        p.choice_h = (rows.extent(RowPitch{m.button_h}) + (rows - kOneRow).extent(RowPitch{m.gap}))
                         .over(0);
        fixed += p.choice_h + (!p.main_count.empty() ? m.gap : 0);
    }
    const std::int32_t room = bottom - m.list_top - fixed;
    p.row_h = m.row_h;
    if (!halves.empty() && room > 0)
        p.row_h = std::min(m.row_h, halves.share_of(kHalvesPerRow * room));
    p.sep_h = hd_sep_h(p.row_h);

    const auto block = [&](RowCount rows, RowCount seps) {
        return rows.empty()
            ? 0
            : ((rows - seps).extent(RowPitch{p.row_h}) + seps.extent(RowPitch{p.sep_h}))
                  .over(frame);
    };
    p.main_y = m.list_top;
    p.main_h = block(p.main_count, p.main_seps);
    p.pinned_h = block(p.pinned_count, p.pinned_seps);
    p.pinned_y = bottom - p.pinned_h;
    p.choice_y = p.main_y + p.main_h + (!p.main_count.empty() ? m.gap : 0);

    const RowCount total = page.total.empty() ? n : page.total;
    p.cue_total = total;
    p.cue = total > n;
    p.cue_up = !page.first.before().empty();
    p.cue_down = page.first.before() + n < total;
    p.cue_at = (page.first + kOneRow).before();

    const RowCount body_total = total - std::min(total, p.pinned_count);
    for (RowIndex row{}; row < end; row += kOneRow) {
        if (!page.row(row).selected)
            continue;
        const bool pinned_row = !p.pinned_count.empty() && row >= p.pinned_first;
        p.cue_at = pinned_row ? body_total + (row - p.pinned_first) + kOneRow
                              : (page.first + row.before()).before() + kOneRow;
    }
    return p;
}

struct HdFit {
    PageDescription page;
    RowIndex top{};
};

[[nodiscard]] constexpr HdFit hd_fit_rows(
    const HdMetrics& m,
    const PageDescription& page,
    std::int32_t pitch,
    RowIndex top
) noexcept {
    using Kind = PageDescription::RowKind;
    HdFit out{page, page.first};
    if (page.shape != PageDescription::Shape::List)
        return out;
    const HdPanelPlan whole = plan_panel(m, page);
    const RowCount body = whole.main_count;
    if (whole.row_h >= pitch || body <= kOneRow)
        return out;
    const RowIndex end = RowIndex{} + page.listed();
    const RowIndex body_end = RowIndex{} + body;
    std::optional<RowIndex> sel;
    for (RowIndex i{}; i < body_end; i += kOneRow)
        if (page.row(i).selected)
            sel = i;
    for (RowCount k = body - kOneRow; k >= kOneRow; k -= kOneRow) {
        RowIndex s = RowIndex{}
            + std::min(top >= page.first ? top - page.first : RowCount{}, body - k);
        if (sel && *sel < s)
            s = *sel;
        if (sel && *sel >= s + k)
            s = *sel - k + kOneRow;
        RowIndex e = s + k;
        const auto rule = [&](RowIndex i) {
            return page.row(i).kind == Kind::Separator;
        };
        while (e - s > kOneRow && rule(s))
            s += kOneRow;
        while (e - s > kOneRow && rule(e - kOneRow))
            e -= kOneRow;
        const RowCount kept = e - s;
        PageDescription cut = page;
        for (RowIndex i{}; i < RowIndex{} + kept; i += kOneRow)
            infra::at_index(cut.rows, i) = page.row(s + i.before());
        RowIndex dst = RowIndex{} + kept;
        for (RowIndex i = body_end; i < end; i += kOneRow, dst += kOneRow)
            infra::at_index(cut.rows, dst) = page.row(i);
        cut.row_count = kept + (page.listed() - body);
        for (RowIndex i = RowIndex{} + cut.row_count; i < end; i += kOneRow)
            infra::at_index(cut.rows, i) = {};
        cut.first = page.first + s.before();
        if (k == kOneRow || plan_panel(m, cut).row_h >= pitch) {
            out.page = cut;
            out.top = cut.first;
            return out;
        }
    }
    return out;
}

}
