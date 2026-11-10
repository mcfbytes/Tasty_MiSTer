// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dips_page.h"

#include <optional>
#include <string>
#include <string_view>

#include "app/hd_window.h"
#include "ui/draw_classic.h"
#include "ui/menu_model.h"
#include "ui/page_ask.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

struct Layout {
    RowIndex lead{};
    RowCount total{};
    RowIndex reset{};
};

Layout layout_for(RowCount count, RowCount rows) noexcept {
    Layout l;

    const RowCount needed = count + kOneRow;
    const bool fits = needed <= rows;
    l.lead = (fits && count < rows - kOneRow)
        ? row_after(saturating_sub(rows - kOneRow, count).half())
        : RowIndex{};

    l.reset = fits ? last_row(rows) : row_after(count);
    l.total = fits ? rows : needed;
    return l;
}

}

std::string dip_row_text(const app::DipLabels::Row& row, proto::DipChoice current) {
    const std::string_view id = current.within(row.choices) ? current.pick(row.ids).view()
                                                            : std::string_view{};
    std::string s;
    s.push_back(' ');
    std::string_view name = row.name.view();
    while (!name.empty() && name.back() == ' ')
        name.remove_suffix(1);
    s.append(name);
    s.push_back(':');
    const std::size_t idlen = id.size();

    if (s.size() + idlen < kClassicRowColumns)
        s.append(kClassicRowColumns - idlen - s.size(), ' ');
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

    const RowCount count = RowCount::of(table_->labels.count);
    const RowCount height = osd_height();
    const Layout l = layout_for(count, height);
    const RowCount total = count + kOneRow;
    const RowIndex selected = sel_ <= reset_row() ? sel_ : RowIndex{};

    const app::HdWindow win = out.window(selected, total);
    const RowCount win_first = win.first.before();
    const RowCount win_count = win.count;
    const RowCount win_last = win_first + win_count;

    const auto row_of = [&](RowIndex at) -> std::optional<RowIndex> {
        if (height.empty())
            return std::nullopt;
        const RowIndex virt = at < reset_row() ? l.lead + at.before() : l.reset;
        if (virt < first_row_)
            return std::nullopt;
        const RowCount offset = virt - first_row_;
        if (offset >= height)
            return std::nullopt;
        return row_after(offset);
    };
    const auto classic_at = [&](RowIndex at) {
        const RowIndex virt = first_row_ + at.before();
        std::string s;
        bool selected_row = false;
        if (virt >= l.lead && virt - l.lead < count) {
            const RowIndex dip_row = row_after(virt - l.lead);
            const proto::DipRowIndex dip_at = dip_row_of(dip_row);
            s = dip_row_text(table_->row(dip_at), table_->current(dip_at));
            selected_row = sel_ == dip_row;
        } else if (virt == l.reset) {
            s = "       Reset to apply";
            selected_row = sel_ == reset_row();
        }
        return classic_row(
            osd_row_at(at.before()),
            s,
            {.inverted = selected_row,
             .stippled = false,
             .leftchar = at == RowIndex{} && first_row_ != RowIndex{}
                 ? std::optional<Glyph>{Glyph::ArrowUp}
                 : std::nullopt,
             .arrow = OsdRenderer::kArrowNone}
        );
    };

    for (const RowIndex i : rows(win_count)) {
        const RowIndex at = i + win_first;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        if (at.before() < count) {
            const proto::DipRowIndex dip_at = dip_row_of(at);
            const app::DipLabels::Row& d = table_->row(dip_at);
            std::string_view name = d.name.view();
            while (!name.empty() && name.back() == ' ')
                name.remove_suffix(1);
            (void)r.hd.label.assign(name);
            r.hd.kind = app::PageDescription::RowKind::Value;
            const proto::DipChoice current = table_->current(dip_at);
            const std::string_view id = current.within(d.choices) ? current.pick(d.ids).view()
                                                                  : std::string_view{};
            (void)r.hd.value.assign(id);
            r.hd.selected = sel_ == at;
        } else {
            r.hd.kind = app::PageDescription::RowKind::Action;
            (void)r.hd.label.assign("Reset to apply");
            r.hd.selected = sel_ == reset_row();
        }
        if (const std::optional<RowIndex> vr = row_of(at))
            r.classic = classic_at(*vr);
    }

    for (const RowIndex r : rows(height)) {
        const RowIndex virt = first_row_ + r.before();
        std::optional<RowIndex> at{};
        if (virt >= l.lead && virt - l.lead < count) {
            at = row_after(virt - l.lead);
        } else if (virt == l.reset) {
            at = reset_row();
        }
        if (at && at->before() >= win_first && at->before() < win_last)
            continue;
        out.rows[out.count++].classic = classic_at(r);
    }
    return true;
}

void DipsPage::track_viewport() noexcept {
    TASTY_SEAT_BODY(DipsPage);
    const RowCount height = osd_height();
    if (height.empty())
        return;
    const Layout l = layout_for(RowCount::of(table_->labels.count), height);
    if (l.total <= height) {
        first_row_ = RowIndex{};
        return;
    }
    const RowIndex virt = sel_ < reset_row() ? l.lead + sel_.before() : l.reset;
    if (sel_ == RowIndex{})
        first_row_ = RowIndex{};
    if (virt < first_row_) {
        first_row_ = virt;
    } else if (virt - first_row_ >= height) {
        first_row_ = virt - (height - kOneRow);
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
        push_op(proto::LinkOp::PulseOption{.bit = proto::kResetBit});
        return Transition{Transition::Kind::CloseAll};
    }
    if (sel_ < reset_row()) {
        const proto::DipRowIndex dip_at = dip_row_of(sel_);
        const app::DipLabels::Row& row = table_->row(dip_at);
        if (row.choices.empty()) {
            ++refusals_;
            return Transition{};
        }

        const proto::DipChoice next = table_->current(dip_at).next(row.choices);
        push_op(proto::LinkOp::SetDip{.row = dip_at, .choice = next});
        return Transition{};
    }
    ++refusals_;
    return Transition{};
}

}
