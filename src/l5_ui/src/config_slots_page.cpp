// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/config_slots_page.h"

#include <array>
#include <string>
#include <utility>

#include "app/hd_window.h"
#include "ui/draw_classic.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr const char kStdBack[] = "            back";
constexpr const char kStartupHeader[] = " Startup config:";
constexpr const char kOtherHeader[] = " Other configs:";

constexpr std::uint32_t kStartupHeaderRow = 1;
constexpr std::uint32_t kSlotZeroRow = 2;
constexpr std::uint32_t kOtherHeaderRow = 4;
constexpr std::uint32_t kOtherFirstRow = 5;

std::string_view plain_label(std::string_view t) noexcept {
    while (!t.empty() && t.front() == ' ')
        t.remove_prefix(1);
    while (!t.empty() && t.back() == ' ')
        t.remove_suffix(1);
    return t;
}

bool blank_text(std::string_view t) noexcept {
    for (char c : t) {
        if (c != ' ')
            return false;
    }
    return true;
}

}

std::uint32_t ConfigSlotsPage::slot_count() const noexcept {
    return table_ != nullptr ? table_->count : 0u;
}

bool ConfigSlotsPage::selectable(std::uint32_t slot) const noexcept {
    if (slot >= slot_count())
        return false;
    if (mode_ == Mode::Save || slot == 0)
        return true;
    return ((table_->exists >> slot) & 1u) != 0;
}

void ConfigSlotsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    asks_ = ctx.ui_requests;
    std::uint64_t m = 0;
    const std::uint32_t n = slot_count();
    for (std::uint32_t i = 0; i < n; ++i) {
        if (selectable(i))
            m |= std::uint64_t{1} << i;
    }
    m |= std::uint64_t{1} << n;
    mask_ = MenuMask{m};
}

void ConfigSlotsPage::ask_slot(cores::ConfigSlot slot) {
    const bool landed = mode_ == Mode::Load
        ? push_ask(asks_, app::UiRequest::LoadCoreConfig{.slot = slot, .scope = {drawn_}})
        : push_ask(asks_, app::UiRequest::SaveCoreConfig{.slot = slot, .scope = {drawn_}});
    if (!landed) {
        ++refusals_;
        return;
    }
    ++published_;
}

std::uint32_t ConfigSlotsPage::slot_at(std::uint32_t row) const noexcept {
    const std::uint32_t rows = osd_height().v;
    if (rows == 0 || row + 1 >= rows)
        return kNoSlot;
    if (row == kSlotZeroRow)
        return 0;
    if (row < kOtherFirstRow)
        return kNoSlot;
    const std::uint32_t slot = row - (kOtherFirstRow - 1u);
    return slot < slot_count() ? slot : kNoSlot;
}

bool ConfigSlotsPage::row_stippled(OsdRow at) const noexcept {
    const std::uint32_t slot = slot_at(at.v);
    return slot != kNoSlot && !selectable(slot);
}

std::string ConfigSlotsPage::row_text(OsdRow at) const {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    const std::uint32_t row = at.v;
    const std::uint32_t rows = osd_height().v;
    if (rows == 0)
        return {};
    if (row == kStartupHeaderRow)
        return kStartupHeader;
    if (row == kOtherHeaderRow)
        return kOtherHeader;
    if (row + 1 == rows)
        return kStdBack;
    const std::uint32_t slot = slot_at(row);
    if (slot == kNoSlot)
        return {};

    if (!selectable(slot))
        return "  ";
    return std::string{"  "} + std::string(table_->preview[slot].view());
}

bool ConfigSlotsPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    (void)out.page.title.assign(mode_ == Mode::Load ? "Load Config" : "Save Config");
    (void)out.page.footer.assign(mode_ == Mode::Load ? "A Load" : "A Save");
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const std::uint32_t rows = osd_height().v;
    const std::uint32_t n = slot_count();

    struct Drawn {
        std::uint32_t row;
        std::string text;
        std::uint32_t slot;
    };

    std::array<Drawn, kMaxPageRows> drawn{};
    std::uint8_t drawn_n = 0;
    std::uint16_t selected = 0;
    for (std::uint32_t r = 0; r < rows; ++r) {
        std::string text = row_text(OsdRow::of(r));
        if (blank_text(text))
            continue;
        Drawn& d = drawn[drawn_n];
        d.row = r;
        d.text = std::move(text);
        d.slot = slot_at(r);
        if (d.slot != kNoSlot && sel_.v == d.slot)
            selected = drawn_n;
        if (d.slot == kNoSlot && r + 1 == rows && sel_.v == n)
            selected = drawn_n;
        ++drawn_n;
    }

    const app::HdWindow win = app::hd_window(selected, drawn_n, out.page.first);
    out.page.first = win.first;
    out.page.total = drawn_n;

    const auto inverted = [&](std::uint32_t row) {
        if (row + 1 == rows)
            return sel_.v == n;
        const std::uint32_t slot = slot_at(row);
        return slot != kNoSlot && sel_.v == slot;
    };

    for (std::uint8_t i = 0; i < win.count; ++i) {
        const std::uint8_t at = static_cast<std::uint8_t>(win.first + i);
        const Drawn& d = drawn[at];
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        (void)r.hd.label.assign(plain_label(d.text));
        if (d.slot != kNoSlot) {
            r.hd.kind = app::PageDescription::RowKind::Action;
            r.hd.selected = sel_.v == d.slot;
        } else if (d.row + 1 == rows) {
            r.hd.kind = app::PageDescription::RowKind::Action;
            r.hd.selected = sel_.v == n;
        } else {
            r.hd.kind = app::PageDescription::RowKind::Text;
        }
        r.classic = classic_row(
            OsdRow::of(d.row),
            d.text,
            inverted(d.row),
            row_stippled(OsdRow::of(d.row)),
            std::nullopt,
            OsdRenderer::kArrowNone
        );
    }

    std::uint8_t di = 0;
    for (std::uint32_t r = 0; r < rows; ++r) {
        const bool drawn_here = di < drawn_n && drawn[di].row == r;
        if (drawn_here && di >= win.first && di < win.first + win.count) {
            ++di;
            continue;
        }
        if (drawn_here)
            ++di;
        out.rows[out.count++].classic = classic_row(
            OsdRow::of(r),
            row_text(OsdRow::of(r)),
            inverted(r),
            row_stippled(OsdRow::of(r)),
            std::nullopt,
            OsdRenderer::kArrowNone
        );
    }
    return true;
}

Transition ConfigSlotsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    if (ev.buttons.has_any({UiEvent::Menu | UiEvent::Left | UiEvent::Back})) {
        return Transition{Transition::Kind::Pop};
    }
    if (!ev.buttons.has_any({UiEvent::Select}))
        return Transition{};

    const std::uint32_t n = slot_count();
    if (sel_.v >= n)
        return Transition{Transition::Kind::Pop};
    if (!selectable(sel_.v)) {
        ++refusals_;
        return Transition{};
    }
    ask_slot(cores::ConfigSlot{static_cast<std::uint8_t>(sel_.v)});
    return Transition{Transition::Kind::CloseAll};
}

}
