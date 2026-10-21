// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cheats_page.h"

#include <cstdio>

#include "app/cheat_store.h"
#include "app/hd_window.h"
#include "app/page_description.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

static_assert(
    static_cast<unsigned char>(0x1A) == 26 && static_cast<unsigned char>(0x1B) == 27,
    "req: the enabled/disabled glyph pair"
);
static_assert(
    static_cast<unsigned>(Glyph::ArrowUp) == 17 && static_cast<unsigned>(Glyph::ArrowDown) == 16,
    "req: the first/last-row scroll arrows, shared with recents"
);
static_assert(
    static_cast<unsigned>(Glyph::SubmenuArrow) == 0x16,
    "req / req: the truncation continuation glyph"
);

namespace {

std::string_view name_of_row(const app::CheatRow& r) noexcept {
    return r.name;
}

bool ends_with_gg(std::string_view s) noexcept {
    if (s.size() < 3)
        return false;
    const std::string_view tail = s.substr(s.size() - 3);
    return (tail[0] == '.')
        && (tail[1] == 'g' || tail[1] == 'G')
        && (tail[2] == 'g' || tail[2] == 'G');
}

constexpr char kOnGlyph = static_cast<char>(0x1A);
constexpr char kOffGlyph = static_cast<char>(0x1B);
constexpr char kMoreGlyph = static_cast<char>(0x16);

}

std::string cheat_row_text(const app::CheatRow& row) {
    std::string s;
    s.push_back(' ');
    s.push_back(row.enabled ? kOnGlyph : kOffGlyph);
    s.push_back(' ');
    s.append(row.name);

    if (s.size() > 3 && ends_with_gg(s))
        s.resize(s.size() - 3);
    if (s.size() > 28) {
        s.resize(28);
        s.push_back(kMoreGlyph);
    }
    return s;
}

std::string cheats_title_text(std::size_t loaded) {
    char b[32];
    std::snprintf(b, sizeof(b), "Cheats (%u)", static_cast<unsigned>(loaded));
    return b;
}

CheatsPage::CheatsPage(app::CheatStore& store) noexcept
    : store_(&store),
      view_(rows_, &name_of_row, ViewportPolicy{}) {}

void CheatsPage::refresh_() {
    TASTY_SEAT_BODY(CheatsPage);
    rows_.clear();
    const std::size_t n = store_->count();
    rows_.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        rows_.push_back(store_->row(i));
    view_.set_items(rows_);
    opens_seen_ = store_->opens();
}

void CheatsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(CheatsPage);
    link_tx_ = ctx.link_tx;
    refresh_();
    view_.set_height(osd_height());
    view_.home();
}

Transition CheatsPage::on_tick(PageContext&) {
    TASTY_SEAT_BODY(CheatsPage);
    if (store_->opens() != opens_seen_ || rows_.size() != store_->count()) {
        refresh_();
        view_.home();
    }
    return Transition{};
}

void CheatsPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(CheatsPage);
    out.set_title(cheats_title_text(store_->loaded()));

    const std::uint32_t rows = osd_height().v;
    const std::uint32_t first = view_.viewport().first.v;
    const std::uint32_t sel = view_.viewport().selected.v;
    const auto count = static_cast<std::uint32_t>(rows_.size());
    for (std::uint32_t r = 0; r < rows; ++r) {
        const std::uint32_t k = first + r;
        std::string s;
        std::uint8_t leftchar = 0;
        if (k < count) {
            s = cheat_row_text(rows_[k]);

            if (r == 0 && k != 0)
                leftchar = static_cast<std::uint8_t>(Glyph::ArrowUp);
            if (r + 1 == rows && k + 1 < count) {
                leftchar = static_cast<std::uint8_t>(Glyph::ArrowDown);
            }
        }
        out.set_arrow(OsdRenderer::kArrowNone);
        out.text(OsdRow{static_cast<std::uint8_t>(r)}, s, k < count && k == sel, false, leftchar);
    }
}

bool CheatsPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(CheatsPage);
    const std::uint16_t hint = out.first;
    out = {};
    (void)out.title.assign(cheats_title_text(store_->loaded()));

    const std::uint16_t total = static_cast<std::uint16_t>(rows_.size());
    const std::uint16_t selected = static_cast<std::uint16_t>(view_.viewport().selected.v);
    const app::HdWindow win = app::hd_window(selected, total, hint);
    out.first = win.first;
    out.row_count = win.count;
    out.total = total;
    for (std::uint8_t i = 0; i < win.count; ++i) {
        const std::uint16_t at = static_cast<std::uint16_t>(win.first + i);
        app::PageDescription::Row& row = out.rows[i];
        (void)row.label.assign(rows_[at].name);
        (void)row.value.assign(rows_[at].enabled ? "On" : "Off");
        row.kind = app::PageDescription::RowKind::Value;
        row.steps = false;
        row.enabled = true;
        row.selected = at == selected;
    }
    return true;
}

void CheatsPage::publish_apply_() {
    TASTY_SEAT_BODY(CheatsPage);

    if (!link_tx_ || !link_tx_->push(proto::LinkOp::ApplyCheats{})) {
        ++refusals_;
        return;
    }
    ++published_;
}

Transition CheatsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(CheatsPage);
    if ((ev.buttons & UiEvent::Menu) != 0) {

        return Transition{Transition::Kind::Pop};
    }
    view_.set_height(osd_height());

    if ((ev.buttons & (UiEvent::Left | UiEvent::Back)) != 0) {
        view_.prev_page();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Right) != 0) {
        view_.next_page();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Down) != 0) {
        view_.next();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Up) != 0) {
        view_.prev();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Select) != 0) {
        const std::size_t i = view_.viewport().selected.v;
        auto changed = store_->toggle(i);
        if (!changed) {
            ++refusals_;
            return Transition{};
        }

        if (*changed) {
            publish_apply_();
        } else {
            ++refusals_;
        }
        rows_[i] = store_->row(i);
        return Transition{};
    }
    return Transition{};
}

}
