// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dips_page.h"

#include <cstring>
#include <string>
#include <vector>

#include "app/hd_window.h"
#include "ui/menu_model.h"
#include "ui/page_ask.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

struct Layout {
    std::uint32_t lead = 0;
    std::uint32_t total = 0;
    std::uint32_t reset = 0;
};

Layout layout_for(std::uint32_t count, std::uint32_t rows) noexcept {
    Layout l;
    const bool fits = count + 1 <= rows;
    l.lead = (fits && rows >= 1 && count < rows - 1) ? (rows - 1 - count) / 2 : 0;

    l.reset = fits ? rows - 1 : count;
    l.total = l.reset + 1;
    return l;
}

}

std::string dip_row_text(const app::DipTable::Row& row) {
    const char* id = row.current < row.choices ? row.ids[row.current] : "";
    std::string s;
    s.push_back(' ');
    std::string_view name{row.name};
    while (!name.empty() && name.back() == ' ')
        name.remove_suffix(1);
    s.append(name);
    s.push_back(':');
    const std::size_t idlen = std::strlen(id);

    if (s.size() + idlen < 28)
        s.append(28 - idlen - s.size(), ' ');
    s.append(id);
    return s;
}

void DipsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(DipsPage);
    link_tx_ = ctx.link_tx;
    asks_ = ctx.ui_requests;
}

void DipsPage::save_dips() {
    TASTY_SEAT_BODY(DipsPage);
    if (!push_ask(asks_, app::UiRequest::SaveDips{})) {
        ++refusals_;
        return;
    }
    ++published_;
}

template <class A>
void DipsPage::push_op(const A& op) {
    TASTY_SEAT_BODY(DipsPage);
    if (!link_tx_ || !link_tx_->push(op)) {
        ++refusals_;
        return;
    }
    ++published_;
}

void DipsPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(DipsPage);
    out.set_title("DIP Switches");
    const std::uint32_t rows = osd_height().v;
    if (rows == 0)
        return;
    const std::uint32_t count = table_->count;
    const Layout l = layout_for(count, rows);

    for (std::uint32_t r = 0; r < rows; ++r) {
        const std::uint32_t virt = r + first_row_;
        std::string s;
        bool selected = false;
        if (virt >= l.lead && virt < l.lead + count) {
            const std::uint32_t i = virt - l.lead;
            s = dip_row_text(table_->rows[i]);
            selected = sel_.v == i;
        } else if (virt == l.reset) {
            s = "       Reset to apply";
            selected = sel_.v == count;
        }
        out.set_arrow(OsdRenderer::kArrowNone);
        out.text(
            OsdRow{static_cast<std::uint8_t>(r)},
            s,
            selected,
            false,
            static_cast<std::uint8_t>(r == 0 && first_row_ != 0 ? 17 : 0)
        );
    }
}

bool DipsPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(DipsPage);
    const std::uint16_t hint = out.first;
    out = {};
    (void)out.title.assign("DIP Switches");

    const std::uint32_t count = table_->count;
    std::vector<app::PageDescription::Row> all;
    all.reserve(count + 1);
    std::uint16_t selected = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const app::DipTable::Row& r = table_->rows[i];
        std::string_view name{r.name};
        while (!name.empty() && name.back() == ' ')
            name.remove_suffix(1);
        app::PageDescription::Row row;
        (void)row.label.assign(name);
        row.kind = app::PageDescription::RowKind::Value;
        const char* id = r.current < r.choices ? r.ids[r.current] : "";
        (void)row.value.assign(id);
        if (sel_.v == i) {
            row.selected = true;
            selected = static_cast<std::uint16_t>(all.size());
        }
        all.push_back(row);
    }
    app::PageDescription::Row reset;
    reset.kind = app::PageDescription::RowKind::Action;
    (void)reset.label.assign("Reset to apply");
    if (sel_.v == count) {
        reset.selected = true;
        selected = static_cast<std::uint16_t>(all.size());
    }
    all.push_back(reset);

    const std::uint16_t total = static_cast<std::uint16_t>(all.size());
    const app::HdWindow win = app::hd_window(selected, total, hint);
    out.first = win.first;
    out.row_count = win.count;
    out.total = total;
    for (std::uint8_t i = 0; i < win.count; ++i) {
        out.rows[i] = all[static_cast<std::size_t>(win.first) + i];
    }
    return true;
}

void DipsPage::track_viewport() noexcept {
    TASTY_SEAT_BODY(DipsPage);
    const std::uint32_t rows = osd_height().v;
    if (rows == 0)
        return;
    const Layout l = layout_for(table_->count, rows);
    if (l.total <= rows) {
        first_row_ = 0;
        return;
    }
    const std::uint32_t virt = sel_.v < table_->count ? l.lead + sel_.v : l.reset;
    if (sel_.v == 0)
        first_row_ = 0;
    if (virt < first_row_) {
        first_row_ = virt;
    } else if (virt >= first_row_ + rows) {
        first_row_ = virt - rows + 1;
    }
}

Transition DipsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(DipsPage);
    const bool back = (ev.buttons & (UiEvent::Menu | UiEvent::Back | UiEvent::Left)) != 0;
    if (back) {

        save_dips();
        return Transition{Transition::Kind::Pop};
    }
    if ((ev.buttons & UiEvent::Select) == 0)
        return Transition{};

    const std::uint32_t count = table_->count;
    if (sel_.v == count) {

        save_dips();
        push_op(proto::LinkOp::PulseOption{.bit = proto::StatusBit{0}});
        return Transition{Transition::Kind::CloseAll};
    }
    if (sel_.v < count) {
        const app::DipTable::Row& row = table_->rows[sel_.v];
        if (row.choices == 0) {
            ++refusals_;
            return Transition{};
        }

        const auto next = static_cast<std::uint32_t>((row.current + 1u) % row.choices);
        push_op(proto::LinkOp::SetDip{.row = static_cast<std::uint8_t>(sel_.v), .choice = next});
        return Transition{};
    }
    ++refusals_;
    return Transition{};
}

}
