// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/classic_options_view.h"

#include <memory>
#include <string>
#include <vector>

#include "app/hd_window.h"
#include "app/page_description.h"
#include "ui/options_model.h"
#include "ui/options_page.h"
#include "ui/options_surface.h"
#include "ui/osd_renderer.h"
#include "ui/ui_event.h"

namespace mister::ui {

using proto::OsdRow;

ClassicOptionsView::ClassicOptionsView(IOptionsSurface& surface) noexcept
    : surface_(surface),
      model_(surface.walk()) {}

PageId ClassicOptionsView::id() const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return PageId::CoreOptions;
}

BuildEnv ClassicOptionsView::walk_env(const BuildEnv& env) const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return env;
}

void ClassicOptionsView::on_walk() {
    TASTY_SEAT_BODY(ClassicOptionsView);
    track_viewport();
}

std::uint32_t ClassicOptionsView::status_field(proto::BitField b) const {
    return surface_.field(b);
}

std::string ClassicOptionsView::compose(const OptionItem& it) const {
    return it.compose(OptionItem::Look{.flat = model_.flat(), .value = status_field(it.bits)});
}

template <class Visit>
void ClassicOptionsView::for_each_drawn(Visit&& visit) const {
    for (const std::unique_ptr<OptionItem>& owned : model_.items()) {
        if (owned->screen_row.has_value())
            visit(*owned);
    }
}

void ClassicOptionsView::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    out.set_title(surface_.heading());

    const std::uint32_t rows = osd_height().v;
    std::vector<std::string> composed(rows);
    for_each_drawn([&](const OptionItem& it) {
        if (it.screen_row->v < first_row_)
            return;
        const std::uint32_t row = it.screen_row->v - first_row_;
        if (row >= rows)
            return;
        composed[row] = compose(it);
    });

    const bool at_root = (model_.page() == 0);
    for (std::uint32_t r = 0; r < rows; ++r) {
        const OptionItem* it = row_at(r);
        const bool selected = it != nullptr
            && it->selectable_index.has_value()
            && *it->selectable_index == sel_;
        const bool stippled = it != nullptr && it->disabled;
        const bool has_ring = at_root && it != nullptr && it->rings_at_root();
        out.set_arrow(
            has_ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight)
                     : OsdRenderer::kArrowNone
        );

        std::uint8_t leftchar = 0;
        if (r == 0 && first_row_ != 0) {
            leftchar = 17;
        } else if (r + 1 == rows && !has_ring) {
            leftchar = 16;
        }
        out.text(OsdRow{static_cast<std::uint8_t>(r)}, composed[r], selected, stippled, leftchar);
    }
}

bool ClassicOptionsView::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    const std::uint16_t hint = out.first;
    out = {};
    (void)out.title.assign(surface_.heading());

    const bool flat = model_.flat();
    std::vector<app::PageDescription::Row> all;
    std::uint16_t selected = 0;
    for_each_drawn([&](const OptionItem& it) {
        const OptionItem::Look look{.flat = flat, .value = status_field(it.bits)};
        app::PageDescription::Row row = it.describe(look);

        if (it.selectable_index.has_value() && *it.selectable_index == sel_) {
            row.selected = true;
            selected = static_cast<std::uint16_t>(all.size());
        }
        all.push_back(row);
    });

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

const OptionItem* ClassicOptionsView::row_at(std::uint32_t row) const {
    const std::uint32_t drawn = row + first_row_;
    for (const std::unique_ptr<OptionItem>& it : model_.items()) {
        if (it->screen_row.has_value() && it->screen_row->v == drawn)
            return it.get();
    }
    return nullptr;
}

void ClassicOptionsView::track_viewport() noexcept {
    TASTY_SEAT_BODY(ClassicOptionsView);
    const std::uint32_t rows = osd_height().v;
    if (rows == 0)
        return;
    if (sel_.v == 0)
        first_row_ = 0;
    const OptionItem* it = model_.at_selectable(sel_);
    if (it == nullptr || !it->screen_row.has_value())
        return;
    const std::uint32_t drawn = it->screen_row->v;
    if (drawn < first_row_) {
        first_row_ = drawn;
    } else if (drawn >= first_row_ + rows) {
        first_row_ = drawn - rows + 1;
    }
}

Transition ClassicOptionsView::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ClassicOptionsView);

    if ((ev.buttons & UiEvent::Menu) != 0) {
        return Transition{Transition::Kind::CloseAll};
    }

    const bool select = (ev.buttons & UiEvent::Select) != 0;
    const bool plus = (ev.buttons & UiEvent::Plus) != 0;
    const bool minus = (ev.buttons & UiEvent::Minus) != 0;
    const bool back = (ev.buttons & UiEvent::Back) != 0;
    const bool right = (ev.buttons & UiEvent::Right) != 0;
    const bool left = (ev.buttons & UiEvent::Left) != 0;
    const bool recent = (ev.buttons & UiEvent::Recent) != 0;
    const bool at_root = (model_.page() == 0);

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

std::uint32_t ClassicOptionsView::first_row() const noexcept {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return first_row_;
}

bool ClassicOptionsView::empty() const {
    TASTY_SEAT_BODY(ClassicOptionsView);
    return model_.empty_page();
}

}
