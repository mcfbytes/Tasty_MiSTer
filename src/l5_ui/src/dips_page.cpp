// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dips_page.h"

#include <cstring>
#include <string>

#include "app/hd_window.h"
#include "ui/draw_classic.h"
#include "ui/menu_model.h"
#include "ui/page_ask.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/page_row.h"
#include "ui/page_window.h"
#include "ui/page_walk.h"
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
    constexpr std::size_t kIdEndColumn = 28;
    const char* id = row.current.within(row.choices) ? row.current.pick(row.ids) : "";
    std::string s;
    s.push_back(' ');
    std::string_view name{row.name};
    while (!name.empty() && name.back() == ' ')
        name.remove_suffix(1);
    s.append(name);
    s.push_back(':');
    const std::size_t idlen = std::strlen(id);

    if (s.size() + idlen < kIdEndColumn)
        s.append(kIdEndColumn - idlen - s.size(), ' ');
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

bool DipsPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(DipsPage);
    (void)out.page.title.assign("DIP Switches");
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const std::uint32_t count = table_->count;
    const std::uint32_t rows = osd_height().v;
    const Layout l = layout_for(count, rows);
    const RowCount total = RowCount::of(count) + kOneRow;
    const RowIndex selected = sel_ <= reset_row() ? sel_ : RowIndex{};

    const app::HdWindow win = window_page(out.page, selected, total);
    const std::uint32_t win_first = win.first;
    const std::uint32_t win_last = win_first + win.count;

    constexpr std::uint32_t kNoPhysicalRow = 0xFFFF'FFFFu;

    const auto row_of = [&](std::uint32_t at) {
        if (rows == 0)
            return kNoPhysicalRow;
        const std::uint32_t virt = at < count ? l.lead + at : l.reset;
        if (virt < first_row_ || virt >= first_row_ + rows)
            return kNoPhysicalRow;
        return virt - first_row_;
    };
    const auto classic_at = [&](std::uint32_t r) {
        const std::uint32_t virt = r + first_row_;
        std::string s;
        bool selected_row = false;
        if (virt >= l.lead && virt < l.lead + count) {
            const std::uint32_t i = virt - l.lead;
            s = dip_row_text(table_->rows[i]);
            selected_row = sel_ == RowIndex::of(i);
        } else if (virt == l.reset) {
            s = "       Reset to apply";
            selected_row = sel_ == reset_row();
        }
        return classic_row(
            OsdRow::of(r),
            s,
            selected_row,
            false,
            r == 0 && first_row_ != 0 ? std::optional<Glyph>{Glyph::ArrowUp} : std::nullopt,
            OsdRenderer::kArrowNone
        );
    };

    for (std::uint8_t i = 0; i < win.count; ++i) {
        const std::uint32_t at = win_first + i;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        if (at < count) {
            const app::DipTable::Row& d = table_->rows[at];
            std::string_view name{d.name};
            while (!name.empty() && name.back() == ' ')
                name.remove_suffix(1);
            (void)r.hd.label.assign(name);
            r.hd.kind = app::PageDescription::RowKind::Value;
            const char* id = d.current.within(d.choices) ? d.current.pick(d.ids) : "";
            (void)r.hd.value.assign(id);
            r.hd.selected = sel_ == RowIndex::of(at);
        } else {
            r.hd.kind = app::PageDescription::RowKind::Action;
            (void)r.hd.label.assign("Reset to apply");
            r.hd.selected = sel_ == reset_row();
        }
        const std::uint32_t vr = row_of(at);
        if (vr != kNoPhysicalRow)
            r.classic = classic_at(vr);
    }

    for (std::uint32_t r = 0; r < rows; ++r) {
        const std::uint32_t virt = r + first_row_;
        std::uint32_t at = kNoPhysicalRow;
        if (virt >= l.lead && virt < l.lead + count) {
            at = virt - l.lead;
        } else if (virt == l.reset) {
            at = count;
        }
        if (at != kNoPhysicalRow && at >= win_first && at < win_last)
            continue;
        out.rows[out.count++].classic = classic_at(r);
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
    const std::uint32_t virt = sel_ < reset_row() ? l.lead + sel_.v : l.reset;
    if (sel_ == RowIndex{})
        first_row_ = 0;
    if (virt < first_row_) {
        first_row_ = virt;
    } else if (virt >= first_row_ + rows) {
        first_row_ = virt - rows + 1;
    }
}

Transition DipsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(DipsPage);
    const bool back = ev.buttons.has_any({UiEvent::Menu | UiEvent::Back | UiEvent::Left});
    if (back) {

        save_dips();
        return Transition{Transition::Kind::Pop};
    }
    if (!ev.buttons.has_any({UiEvent::Select}))
        return Transition{};

    if (sel_ == reset_row()) {

        save_dips();
        push_op(proto::LinkOp::PulseOption{.bit = proto::StatusBit{0}});
        return Transition{Transition::Kind::CloseAll};
    }
    if (sel_ < reset_row()) {
        const RowSpan<const app::DipTable::Row> dip_rows{table_->rows};
        const app::DipTable::Row& row = dip_rows[sel_];
        if (row.choices.empty()) {
            ++refusals_;
            return Transition{};
        }

        const proto::DipChoice next = row.current.next(row.choices);
        push_op(proto::LinkOp::SetDip{.row = dip_row_of(sel_), .choice = next});
        return Transition{};
    }
    ++refusals_;
    return Transition{};
}

}
