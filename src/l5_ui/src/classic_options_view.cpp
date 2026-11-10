// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/classic_options_view.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app/page_description.h"
#include "ui/draw_classic.h"
#include "ui/options_model.h"
#include "ui/options_keys.h"
#include "ui/option_item.h"
#include "ui/options_surface.h"
#include "ui/osd_renderer.h"
#include "ui/page_walk.h"
#include "ui/ui_event.h"

namespace mister::ui {

ClassicOptionsView::ClassicOptionsView(IOptionsSurface& surface) noexcept
    : surface_(surface),
      model_(surface.model()) {}

PageId ClassicOptionsView::id() const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return PageId::CoreOptions;
}

BuildEnv ClassicOptionsView::walk_env(const BuildEnv& env) const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return env;
}

void ClassicOptionsView::on_model_rebuilt() {
    TASTY_SEAT_BODY(ClassicOptionsView);
    track_viewport();
}

proto::FieldValue ClassicOptionsView::status_field(proto::BitField b) const {
    return surface_.field(b);
}

std::string ClassicOptionsView::compose(const OptionItem& it) const {
    return it.compose(OptionItem::Look{.flat = model_.flat(), .value = status_field(it.bits)});
}

std::optional<Glyph> ClassicOptionsView::leftchar_of(OsdRow row, bool has_ring) const noexcept {
    if (row == proto::OsdRow{} && first_row_ != RowIndex{})
        return Glyph::ArrowUp;
    if (row == last_osd_row(osd_height()) && !has_ring)
        return Glyph::ArrowDown;
    return std::nullopt;
}

template <class Visit>
void ClassicOptionsView::for_each_drawn(Visit&& visit) const {
    for (const std::unique_ptr<OptionItem>& owned : model_.items()) {
        if (owned->screen_row.has_value())
            visit(*owned);
    }
}

ClassicOptionsView::DrawnList ClassicOptionsView::compose_drawn() const {
    DrawnList list{};
    const bool flat = model_.flat();
    for_each_drawn([&](const OptionItem& it) {
        const OptionItem::Look look{.flat = flat, .value = status_field(it.bits)};
        const std::optional<app::PageDescription::Row> own = it.describe(look);

        if (!own)
            return;
        app::PageDescription::Row row = *own;

        if (it.selectable_index.has_value() && *it.selectable_index == sel_) {
            row.selected = true;
            list.selected = RowIndex::of(list.rows.size());
        }
        list.rows.push_back(Drawn{.hd = row, .item = infra::OptRef<const OptionItem>{it}});
    });
    return list;
}

void ClassicOptionsView::compose_lines(RowSpan<Line> lines) const {
    const RowCount rows = osd_height();
    for_each_drawn([&](const OptionItem& it) {
        const RowIndex placed = row_index_of(*it.screen_row);
        if (placed < first_row_)
            return;
        const RowCount row = placed - first_row_;
        if (row >= rows)
            return;
        lines[row_after(row)].text = compose(it);
    });
}

bool ClassicOptionsView::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    (void)out.page.title.assign(surface_.heading());
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowCount rows = osd_height();
    const bool at_root = (model_.page() == proto::kRootSubmenu);
    const DrawnList drawn = compose_drawn();

    const RowSpan<const Drawn> drawn_rows{std::span<const Drawn>{drawn.rows}};
    const RowCount total = RowCount::of(drawn.rows.size());
    const app::HdWindow win = out.window(drawn.selected, total);
    out.page.row_count = win.count;

    std::vector<Line> storage(rows.size());
    const RowSpan<Line> lines{std::span<Line>{storage}};
    compose_lines(lines);

    for (const RowIndex at : ui::rows(win.first, win.count)) {
        if (at >= row_after(total) || out.count >= out.rows.size())
            break;
        PageRow& r = out.rows[out.count++];
        r.hd = drawn_rows[at].hd;
        r.has_hd = true;
        const OptionItem& window_item = drawn_rows[at].item.value();
        if (!window_item.screen_row.has_value()
            || row_index_of(*window_item.screen_row) < first_row_) {
            continue;
        }
        const RowCount phys = row_index_of(*window_item.screen_row) - first_row_;
        if (phys >= rows)
            continue;
        Line& line = lines[row_after(phys)];
        if (line.emitted)
            continue;
        line.emitted = true;
        const infra::OptRef<const OptionItem> it = row_at(phys);
        const bool has_ring = at_root && it && it->rings_at_root();
        r.classic = classic_row(
            osd_row_at(phys),
            line.text,
            {.inverted = it && it->selectable_index.has_value() && *it->selectable_index == sel_,
             .stippled = it && it->disabled,
             .leftchar = leftchar_of(osd_row_at(phys), has_ring),
             .arrow = has_ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight)
                               : OsdRenderer::kArrowNone}
        );
    }

    for (RowCount r{}; r < rows; r += kOneRow) {
        Line& line = lines[row_after(r)];
        if (line.emitted || out.count >= out.rows.size())
            continue;
        const infra::OptRef<const OptionItem> it = row_at(r);
        const bool has_ring = at_root && it && it->rings_at_root();
        PageRow& pr = out.rows[out.count++];
        pr.classic = classic_row(
            osd_row_at(r),
            line.text,
            {.inverted = it && it->selectable_index.has_value() && *it->selectable_index == sel_,
             .stippled = it && it->disabled,
             .leftchar = leftchar_of(osd_row_at(r), has_ring),
             .arrow = has_ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight)
                               : OsdRenderer::kArrowNone}
        );
        pr.has_hd = false;
    }
    return true;
}

infra::OptRef<const OptionItem> ClassicOptionsView::row_at(RowCount row) const {
    const RowIndex drawn = first_row_ + row;
    for (const std::unique_ptr<OptionItem>& it : model_.items()) {
        if (it->screen_row.has_value() && row_index_of(*it->screen_row) == drawn) {
            return infra::OptRef<const OptionItem>{*it};
        }
    }
    return {};
}

void ClassicOptionsView::track_viewport() noexcept {
    TASTY_SEAT_BODY(ClassicOptionsView);
    const RowCount rows = osd_height();
    if (rows.empty())
        return;
    if (sel_ == RowIndex{})
        first_row_ = RowIndex{};
    const infra::OptRef<const OptionItem> it = model_.at_selectable(sel_);
    if (!it || !it->screen_row.has_value())
        return;
    const RowIndex drawn = row_index_of(*it->screen_row);
    if (drawn < first_row_) {
        first_row_ = drawn;
    } else if (drawn >= first_row_ + rows) {
        first_row_ = drawn - rows + kOneRow;
    }
}

Transition ClassicOptionsView::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ClassicOptionsView);
    const OptionsKeys keys = OptionsKeys::of(ev);

    if (keys.menu) {
        return Transition{Transition::Kind::CloseAll};
    }

    const bool at_root = (model_.page() == proto::kRootSubmenu);

    if (keys.back || (keys.left && !at_root)) {
        return at_root ? Transition{Transition::Kind::Leave} : Transition{Transition::Kind::Pop};
    }

    if (keys.recent && !keys.select && !keys.plus && !keys.minus) {
        const infra::OptRef<const OptionItem> it = model_.at_selectable(sel_);
        if (it && !it->disabled && it->opens_browser()) {
            return surface_.open_recents(*it);
        }
        surface_.refuse();
        return Transition{};
    }
    if (keys.no_row_key()) {
        if (const std::optional<Transition> ring = ring_transition(at_root, keys))
            return *ring;
        return Transition{};
    }

    const infra::OptRef<const OptionItem> it = model_.at_selectable(sel_);
    if (!it) {
        surface_.refuse();
        return Transition{};
    }

    if (it->disabled) {
        surface_.refuse();
        return Transition{};
    }

    if (keys.select)
        return it->select(surface_);
    return it->step(keys.minus ? CycleDir::Back : CycleDir::Next, status_field(it->bits), surface_);
}

MenuMask ClassicOptionsView::mask() const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return model_.mask();
}

void ClassicOptionsView::set_selection(RowIndex i) {
    TASTY_SEAT_BODY(ClassicOptionsView);
    sel_ = i;
    track_viewport();
}

RowIndex ClassicOptionsView::selection() const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return sel_;
}

RowIndex ClassicOptionsView::first_row() const noexcept {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return first_row_;
}

bool ClassicOptionsView::empty() const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return model_.empty_page();
}

}
