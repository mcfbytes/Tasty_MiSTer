// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/confirm_page.h"

#include "app/hd_panel_plan.h"
#include "app/hd_window.h"
#include "app/page_description.h"
#include "ui/confirm_host.h"
#include "ui/osd_renderer.h"
#include "ui/ui_event.h"

namespace mister::ui {

void ConfirmPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(ConfirmPage);
    out.set_title(spec_.title);
    std::uint8_t row = 0;
    for (std::string_view line : spec_.body) {
        out.text(proto::OsdRow{row}, line);
        ++row;
    }
    for (std::size_t i = 0; i < spec_.choices.size(); ++i) {
        out.text(proto::OsdRow{row}, spec_.choices[i], sel_.v == i);
        ++row;
    }
}

bool ConfirmPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(ConfirmPage);
    const std::uint16_t hint = out.first;
    out = {};
    (void)out.title.assign(spec_.title);
    out.shape = app::PageDescription::Shape::Choice;

    const std::uint16_t body_n = static_cast<std::uint16_t>(spec_.body.size());
    const std::uint16_t choices_n = static_cast<std::uint16_t>(spec_.choices.size());
    const std::uint16_t total = static_cast<std::uint16_t>(body_n + choices_n);
    const std::uint16_t selected = choices_n == 0 ? 0 : static_cast<std::uint16_t>(body_n + sel_.v);

    const app::HdWindow win = app::hd_window(selected, total, hint);
    out.first = win.first;
    out.row_count = win.count;
    out.total = total;
    for (std::uint8_t i = 0; i < win.count; ++i) {
        const std::uint16_t at = static_cast<std::uint16_t>(win.first + i);
        app::PageDescription::Row& row = out.rows[i];
        if (at < body_n) {
            row.kind = app::PageDescription::RowKind::Text;
            (void)row.label.assign(spec_.body[at]);
        } else {
            row.kind = app::PageDescription::RowKind::Action;
            (void)row.label.assign(spec_.choices[at - body_n]);
            row.selected = choices_n != 0 && at == selected;
        }
    }
    return true;
}

Transition ConfirmPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ConfirmPage);
    if (!spec_.host)
        return Transition{Transition::Kind::Pop};
    if ((ev.buttons & UiEvent::Menu) != 0)
        return spec_.host->on_cancel();
    if ((ev.buttons & UiEvent::Select) != 0) {
        return spec_.host->on_confirm(static_cast<std::uint8_t>(sel_.v));
    }

    const std::size_t n = spec_.choices.size();
    if (n != 0 && app::choices_in_a_row(n)) {
        if ((ev.buttons & UiEvent::Left) != 0 && sel_.v > 0) {
            set_selection(RowIndex{sel_.v - 1});
        } else if ((ev.buttons & UiEvent::Right) != 0 && sel_.v + 1 < n) {
            set_selection(RowIndex{sel_.v + 1});
        }
    }
    return Transition{};
}

}
