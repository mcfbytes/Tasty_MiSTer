// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/classic_options_view.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/hd_window.h"
#include "app/page_description.h"
#include "ui/draw_classic.h"
#include "ui/options_model.h"
#include "ui/options_page.h"
#include "ui/options_surface.h"
#include "ui/osd_renderer.h"
#include "ui/page_walk.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {}

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

bool ClassicOptionsView::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    (void)out.page.title.assign(surface_.heading());
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowCount rows = osd_height();
    const bool at_root = (model_.page() == proto::kRootSubmenu);
    const bool flat = model_.flat();

    std::vector<app::PageDescription::Row> all;
    std::vector<const OptionItem*> drawn;
    std::uint16_t selected = 0;
    for_each_drawn([&](const OptionItem& it) {
        const OptionItem::Look look{.flat = flat, .value = status_field(it.bits)};
        const std::optional<app::PageDescription::Row> own = it.describe(look);

        if (!own)
            return;
        app::PageDescription::Row row = *own;

        if (it.selectable_index.has_value() && *it.selectable_index == sel_) {
            row.selected = true;
            selected = static_cast<std::uint16_t>(all.size());
        }
        all.push_back(row);
        drawn.push_back(&it);
    });

    const std::uint16_t total = static_cast<std::uint16_t>(all.size());
    const app::HdWindow win = app::hd_window(selected, total, out.page.first);
    out.page.first = win.first;
    out.page.row_count = win.count;
    out.page.total = total;

    std::vector<std::string> composed(rows.size());
    for_each_drawn([&](const OptionItem& it) {
        const RowIndex placed = row_index_of(*it.screen_row);
        if (placed < first_row_)
            return;
        const RowCount row = placed - first_row_;
        if (row >= rows)
            return;
        composed[row.size()] = compose(it);
    });
    std::vector<bool> emitted(rows.size(), false);

    for (std::uint8_t i = 0; i < win.count; ++i) {
        const std::uint32_t index = static_cast<std::uint32_t>(win.first) + i;
        if (index >= all.size() || out.count >= out.rows.size())
            break;
        PageRow& r = out.rows[out.count++];
        r.hd = all[index];
        r.has_hd = true;
        const OptionItem& window_item = *drawn[index];
        if (!window_item.screen_row.has_value()
            || row_index_of(*window_item.screen_row) < first_row_) {
            continue;
        }
        const RowCount phys = row_index_of(*window_item.screen_row) - first_row_;
        if (phys >= rows || emitted[phys.size()])
            continue;
        emitted[phys.size()] = true;
        const OptionItem* it = row_at(phys);
        const bool has_ring = at_root && it != nullptr && it->rings_at_root();
        r.classic = classic_row(
            osd_row_at(phys),
            composed[phys.size()],
            it != nullptr && it->selectable_index.has_value() && *it->selectable_index == sel_,
            it != nullptr && it->disabled,
            leftchar_of(osd_row_at(phys), has_ring),
            has_ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight)
                     : OsdRenderer::kArrowNone
        );
    }

    for (RowCount r{}; r < rows; r += kOneRow) {
        if (emitted[r.size()] || out.count >= out.rows.size())
            continue;
        const OptionItem* it = row_at(r);
        const bool has_ring = at_root && it != nullptr && it->rings_at_root();
        PageRow& pr = out.rows[out.count++];
        pr.classic = classic_row(
            osd_row_at(r),
            composed[r.size()],
            it != nullptr && it->selectable_index.has_value() && *it->selectable_index == sel_,
            it != nullptr && it->disabled,
            leftchar_of(osd_row_at(r), has_ring),
            has_ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight)
                     : OsdRenderer::kArrowNone
        );
        pr.has_hd = false;
    }
    return true;
}

const OptionItem* ClassicOptionsView::row_at(RowCount row) const {
    const RowIndex drawn = first_row_ + row;
    for (const std::unique_ptr<OptionItem>& it : model_.items()) {
        if (it->screen_row.has_value() && row_index_of(*it->screen_row) == drawn)
            return it.get();
    }
    return nullptr;
}

void ClassicOptionsView::track_viewport() noexcept {
    TASTY_SEAT_BODY(ClassicOptionsView);
    const RowCount rows = osd_height();
    if (rows.empty())
        return;
    if (sel_ == RowIndex{})
        first_row_ = RowIndex{};
    const OptionItem* it = model_.at_selectable(sel_);
    if (it == nullptr || !it->screen_row.has_value())
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

    if (ev.buttons.has_any({UiEvent::Menu})) {
        return Transition{Transition::Kind::CloseAll};
    }

    const bool select = ev.buttons.has_any({UiEvent::Select});
    const bool plus = ev.buttons.has_any({UiEvent::Plus});
    const bool minus = ev.buttons.has_any({UiEvent::Minus});
    const bool back = ev.buttons.has_any({UiEvent::Back});
    const bool right = ev.buttons.has_any({UiEvent::Right});
    const bool left = ev.buttons.has_any({UiEvent::Left});
    const bool recent = ev.buttons.has_any({UiEvent::Recent});
    const bool at_root = (model_.page() == proto::kRootSubmenu);

    if (back || (left && !at_root)) {
        return at_root ? Transition{Transition::Kind::Leave} : Transition{Transition::Kind::Pop};
    }

    if (recent && !select && !plus && !minus) {
        const OptionItem* it = model_.at_selectable(sel_);
        if (it != nullptr && !it->disabled && it->opens_browser()) {
            return surface_.open_recents(*it);
        }
        surface_.refuse();
        return Transition{};
    }
    if (!select && !plus && !minus) {
        if (at_root && right) {
            return Transition{Transition::Kind::Replace, PageId::System};
        }
        if (at_root && left) {
            return Transition{Transition::Kind::Replace, PageId::Misc, ItemId{3}};
        }
        return Transition{};
    }

    const OptionItem* it = model_.at_selectable(sel_);
    if (it == nullptr) {
        surface_.refuse();
        return Transition{};
    }

    if (it->disabled) {
        surface_.refuse();
        return Transition{};
    }

    const OptionItem::Press press{
        .select = select,
        .minus = minus,
        .at_root = at_root,
        .value = status_field(it->bits)
    };
    return it->activate(press, surface_);
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
