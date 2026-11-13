// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/script_run_page.h"

#include <algorithm>
#include <span>
#include <string>

#include "app/page_description.h"
#include "ui/draw_classic.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/script_launch.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr RowCount kLegendRows{2};
constexpr RowCount kOutputRows = kFullOsdRows - kLegendRows;
constexpr std::string_view kRunningFooter = "           Cancel";
}

ScriptRunPage::ScriptRunPage(ScriptRequest request) noexcept
    : request_(static_cast<ScriptRequest&&>(request)) {}

void ScriptRunPage::on_enter(PageContext&) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (!runner_.start(request_))
        refusals_.add();
}

bool ScriptRunPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ScriptRunPage);
    (void)out.page.title.assign(runner_.title());
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowSpan<const std::string> lines{runner_.lines()};
    const RowCount line_count = lines.size();
    const RowCount hd_count = std::min(line_count, RowCount::of(app::PageDescription::kMaxRows));
    const RowIndex hd_first = row_after(line_count - hd_count);
    out.page.first = hd_first;
    out.page.total = line_count;

    std::string_view word = runner_.finished() ? std::string_view{"OK"} : kRunningFooter;
    word.remove_prefix(std::min(word.find_first_not_of(' '), word.size()));
    std::string legend{"A "};
    legend += word;
    (void)out.page.footer.assign(legend);

    const RowCount first = saturating_sub(line_count, kOutputRows);

    for (const RowIndex i : rows(hd_count)) {
        const RowIndex idx = hd_first + i.before();
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd.kind = app::PageDescription::RowKind::Text;
        (void)r.hd.label.assign(lines[idx]);
        const RowCount vr = idx - row_after(first);
        if (vr >= kOutputRows)
            continue;
        r.classic = classic_row(osd_row_at(vr), lines[idx], {.arrow = OsdRenderer::kArrowNone});
    }

    for (const RowIndex r : rows(kOutputRows)) {
        const RowIndex idx = row_after(first) + r.before();
        const bool listed = idx < row_after(line_count);
        if (listed && idx >= hd_first)
            continue;
        out.rows[out.count++].classic = classic_row(
            osd_row_at(r.before()),
            listed ? std::string_view{lines[idx]} : std::string_view{},
            {.arrow = OsdRenderer::kArrowNone}
        );
    }
    out.rows[out.count++].classic = classic_row(osd_row_at(kOutputRows), std::string_view{});

    const std::string_view footer = runner_.finished() ? std::string_view{"             OK"}
                                                       : kRunningFooter;
    out.rows[out.count++]
        .classic = classic_row(last_osd_row(kFullOsdRows), footer, {.inverted = true});
    return true;
}

Transition ScriptRunPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (!ev.buttons.has_any({UiEvent::Select | UiEvent::Menu | UiEvent::Back})) {
        return Transition{};
    }
    if (runner_.finished())
        return Transition{};

    (void)runner_.cancel();
    return exit_transition();
}

Transition ScriptRunPage::on_tick(PageContext&) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (runner_.finished())
        return Transition{};
    (void)runner_.pump();
    if (runner_.finished())
        return exit_transition();
    return Transition{};
}

Transition ScriptRunPage::exit_transition() const {
    return Transition::pop();
}

}
