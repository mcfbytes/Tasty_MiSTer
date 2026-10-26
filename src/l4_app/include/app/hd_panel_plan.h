// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

#include "app/hd_metrics.h"
#include "app/page_description.h"

namespace mister::app {

[[nodiscard]] constexpr bool choices_in_a_row(std::size_t n) noexcept {
    return n <= 2;
}

struct HdPanelPlan {
    std::uint8_t main_count = 0;
    std::uint8_t main_seps = 0;
    std::uint8_t pinned_first = 0;
    std::uint8_t pinned_count = 0;
    std::uint8_t pinned_seps = 0;
    std::uint8_t choice_first = 0;
    std::uint8_t choice_count = 0;
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
    std::uint16_t cue_at = 0;
    std::uint16_t cue_total = 0;
    bool steps = false;
};

[[nodiscard]] constexpr HdPanelPlan plan_panel(
    const HdMetrics& m,
    const PageDescription& page
) noexcept {
    using Kind = PageDescription::RowKind;
    HdPanelPlan p{};
    const auto n = static_cast<std::uint8_t>(
        std::min<std::size_t>(page.row_count, PageDescription::kMaxRows)
    );
    const bool choice = page.shape == PageDescription::Shape::Choice;
    std::uint8_t body_end = n;
    if (choice) {
        body_end = 0;
        while (body_end < n && page.rows[body_end].kind == Kind::Text)
            ++body_end;
        p.choice_first = body_end;
        p.choice_count = static_cast<std::uint8_t>(n - body_end);
        p.choice_stacked = !choices_in_a_row(p.choice_count);
    } else {
        const auto pinned = std::min<std::uint8_t>(page.pinned, n);
        p.pinned_first = static_cast<std::uint8_t>(n - pinned);
        p.pinned_count = pinned;
        body_end = p.pinned_first;
        if (pinned != 0 && body_end != 0 && page.rows[body_end - 1].kind == Kind::Separator)
            --body_end;
    }
    p.main_count = body_end;
    for (std::uint8_t i = 0; i < n; ++i) {
        const bool sep = page.rows[i].kind == Kind::Separator;
        if (sep && i < body_end)
            ++p.main_seps;
        if (sep && i >= p.pinned_first && p.pinned_count != 0)
            ++p.pinned_seps;
        if (page.rows[i].selected && page.rows[i].steps)
            p.steps = true;
    }

    const std::int32_t frame = 2 * (m.card_pad + m.stroke);
    const std::int32_t bottom = m.help_top - m.gap;
    std::int32_t fixed = 0;
    std::int32_t halves = 2 * (p.main_count - p.main_seps) + p.main_seps;
    if (p.main_count != 0)
        fixed += frame;
    if (p.pinned_count != 0) {
        fixed += frame + m.gap;
        halves += 2 * (p.pinned_count - p.pinned_seps) + p.pinned_seps;
    }
    if (p.choice_count != 0) {
        const std::int32_t rows = p.choice_stacked ? p.choice_count : 1;
        p.choice_h = rows * m.button_h + (rows - 1) * m.gap;
        fixed += p.choice_h + (p.main_count != 0 ? m.gap : 0);
    }
    const std::int32_t room = bottom - m.list_top - fixed;
    p.row_h = m.row_h;
    if (halves > 0 && room > 0)
        p.row_h = std::min(m.row_h, 2 * room / halves);
    p.sep_h = p.row_h / 2;

    const auto block = [&](std::uint8_t rows, std::uint8_t seps) {
        return rows == 0 ? 0 : frame + (rows - seps) * p.row_h + seps * p.sep_h;
    };
    p.main_y = m.list_top;
    p.main_h = block(p.main_count, p.main_seps);
    p.pinned_h = block(p.pinned_count, p.pinned_seps);
    p.pinned_y = bottom - p.pinned_h;
    p.choice_y = p.main_y + p.main_h + (p.main_count != 0 ? m.gap : 0);

    const std::uint16_t total = page.total == 0 ? n : page.total;
    p.cue_total = total;
    p.cue = total > n;
    p.cue_up = page.first > 0;
    p.cue_down = page.first + n < total;
    p.cue_at = static_cast<std::uint16_t>(page.first + 1);

    const auto body_total = static_cast<std::uint16_t>(
        total - std::min<std::uint16_t>(total, p.pinned_count)
    );
    for (std::uint8_t i = 0; i < n; ++i) {
        if (!page.rows[i].selected)
            continue;
        const bool pinned_row = p.pinned_count != 0 && i >= p.pinned_first;
        p.cue_at = static_cast<std::uint16_t>(
            pinned_row ? body_total + (i - p.pinned_first) + 1 : page.first + i + 1
        );
    }
    return p;
}

}
