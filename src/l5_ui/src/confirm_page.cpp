// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/confirm_page.h"

#include "app/hd_panel_plan.h"
#include "app/hd_window.h"
#include "app/page_description.h"
#include "ui/confirm_host.h"
#include "ui/draw_classic.h"
#include "ui/osd_renderer.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/ui_event.h"

namespace mister::ui {

bool ConfirmPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ConfirmPage);
    (void)out.page.title.assign(spec_.title);
    out.page.shape = app::PageDescription::Shape::Choice;
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowSpan<const std::string_view> body{spec_.body};
    const RowSpan<const std::string_view> choices{spec_.choices};
    const RowCount total = body.size() + choices.size();
    const RowIndex selected = choices.empty() ? RowIndex{} : row_after(body.size()) + sel_.before();

    const app::HdWindow win = out.window(selected, total);

    const auto choice_of = [&](RowIndex at) {
        return RowIndex{} + (at - row_after(body.size()));
    };

    const auto classic_at = [&](RowIndex at) {
        if (at < row_after(body.size())) {
            return classic_row(
                osd_row_at(at.before()),
                body[at],
                {.arrow = OsdRenderer::kArrowNone}
            );
        }
        const RowIndex i = choice_of(at);
        return classic_row(
            osd_row_at(at.before()),
            choices[i],
            {.inverted = sel_ == i, .arrow = OsdRenderer::kArrowNone}
        );
    };

    const RowRange window = rows(win.first, win.count);
    for (const RowIndex at : window) {
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        if (at < row_after(body.size())) {
            r.hd.kind = app::PageDescription::RowKind::Text;
            (void)r.hd.label.assign(body[at]);
        } else {
            r.hd.kind = app::PageDescription::RowKind::Action;
            (void)r.hd.label.assign(choices[choice_of(at)]);
            r.hd.selected = !choices.empty() && at == selected;
        }
        r.classic = classic_at(at);
    }
    for (const RowIndex at : rows(total)) {
        if (window.contains(at))
            continue;
        if (static_cast<std::size_t>(out.count) >= out.rows.size())
            break;
        out.rows[out.count++].classic = classic_at(at);
    }
    return true;
}

Transition ConfirmPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ConfirmPage);
    if (!spec_.host)
        return Transition{Transition::Kind::Pop};
    if (ev.buttons.has_any({UiEvent::Menu}))
        return spec_.host->on_cancel();
    if (ev.buttons.has_any({UiEvent::Select})) {
        return spec_.host->on_confirm(sel_);
    }

    const std::size_t n = spec_.choices.size();
    if (n != 0 && app::choices_in_a_row(n)) {
        if (ev.buttons.has_any({UiEvent::Left}) && sel_ > RowIndex{}) {
            set_selection(sel_ - kOneRow);
        } else if (
            ev.buttons.has_any({UiEvent::Right}) && sel_ + kOneRow < row_after(RowCount::of(n))
        ) {
            set_selection(sel_ + kOneRow);
        }
    }
    return Transition{};
}

}
