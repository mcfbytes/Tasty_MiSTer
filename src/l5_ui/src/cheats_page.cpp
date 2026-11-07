// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cheats_page.h"

#include <cstdio>
#include <span>

#include "app/cheat_store.h"
#include "infra/ascii.h"
#include "ui/page_window.h"
#include "app/page_description.h"
#include "ui/draw_classic.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
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

constexpr std::string_view kGgSuffix = ".gg";

bool ends_with_gg(std::string_view s) noexcept {
    return infra::iends_with(s, kGgSuffix);
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

    if (s.size() > kGgSuffix.size() && ends_with_gg(s))
        s.resize(s.size() - kGgSuffix.size());
    if (s.size() > kClassicRowColumns) {
        s.resize(kClassicRowColumns);
        s.push_back(kMoreGlyph);
    }
    return s;
}

std::string CheatsPage::title_text() const {
    TASTY_SEAT_BODY(CheatsPage);
    char b[32];
    std::snprintf(b, sizeof(b), "Cheats (%s)", store_->loaded().text().c_str());
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

bool CheatsPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(CheatsPage);
    (void)out.page.title.assign(title_text());
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowCount height = osd_height();
    const RowIndex first = view_.viewport().first;
    const RowIndex sel = view_.viewport().selected;
    const RowSpan<const app::CheatRow> cheat_rows{std::span<const app::CheatRow>{rows_}};
    const RowCount count = RowCount::of(rows_.size());
    const RowIndex end = row_after(count);
    const app::HdWindow win = window_page(out.page, sel, count);
    const RowRange window = rows(win.first, win.count);
    const RowRange viewport = rows(first, height);

    const auto classic_at = [&](RowCount r) {
        const RowIndex k = first + r;
        std::string s;
        std::optional<Glyph> leftchar;
        if (k < end) {
            s = cheat_row_text(cheat_rows[k]);
            if (r.empty() && k != RowIndex{})
                leftchar = Glyph::ArrowUp;
            if (r + kOneRow == height && k + kOneRow < end) {
                leftchar = Glyph::ArrowDown;
            }
        }
        return classic_row(
            osd_row_at(r),
            s,
            {.inverted = k < end && k == sel,
             .stippled = false,
             .leftchar = leftchar,
             .arrow = OsdRenderer::kArrowNone}
        );
    };

    for (const RowIndex at : window) {
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        (void)r.hd.label.assign(cheat_rows[at].name);
        (void)r.hd.value.assign(cheat_rows[at].enabled ? "On" : "Off");
        r.hd.kind = app::PageDescription::RowKind::Value;
        r.hd.steps = false;
        r.hd.enabled = true;
        r.hd.selected = at == sel;
        if (!viewport.contains(at))
            continue;
        r.classic = classic_at(at - first);
    }
    for (const RowIndex v : rows(height)) {
        const RowCount r = v.before();
        if (first + r < end && window.contains(first + r))
            continue;
        out.rows[out.count++].classic = classic_at(r);
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
    if (ev.buttons.has_any({UiEvent::Menu})) {

        return Transition{Transition::Kind::Pop};
    }
    view_.set_height(osd_height());

    if (ev.buttons.has_any({UiEvent::Left | UiEvent::Back})) {
        view_.prev_page();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Right})) {
        view_.next_page();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Down})) {
        view_.next();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Up})) {
        view_.prev();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Select})) {
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
