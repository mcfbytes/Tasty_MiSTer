// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/config_slots_page.h"

#include <string>
#include <vector>

#include "app/hd_window.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"
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

bool ConfigSlotsPage::row_stippled(std::uint32_t row) const noexcept {
    const std::uint32_t slot = slot_at(row);
    return slot != kNoSlot && !selectable(slot);
}

std::string ConfigSlotsPage::row_text(std::uint32_t row) const {
    TASTY_SEAT_BODY(ConfigSlotsPage);
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

void ConfigSlotsPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    out.set_title(mode_ == Mode::Load ? "Load Config" : "Save Config");
    const std::uint32_t rows = osd_height().v;
    if (rows == 0)
        return;
    const std::uint32_t n = slot_count();

    for (std::uint32_t r = 0; r < rows; ++r) {
        const std::uint32_t slot = slot_at(r);
        const bool selected = (r + 1 == rows) ? sel_.v == n : (slot != kNoSlot && sel_.v == slot);
        out.set_arrow(OsdRenderer::kArrowNone);
        out.text(OsdRow{static_cast<std::uint8_t>(r)}, row_text(r), selected, row_stippled(r), 0);
    }
}

bool ConfigSlotsPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    const std::uint16_t hint = out.first;
    out = {};
    (void)out.title.assign(mode_ == Mode::Load ? "Load Config" : "Save Config");
    (void)out.footer.assign(mode_ == Mode::Load ? "A Load" : "A Save");

    const std::uint32_t rows = osd_height().v;
    const std::uint32_t n = slot_count();
    std::vector<app::PageDescription::Row> all;
    std::uint16_t selected = 0;
    for (std::uint32_t r = 0; r < rows; ++r) {
        const std::string text = row_text(r);
        if (blank_text(text))
            continue;
        const std::uint32_t slot = slot_at(r);
        app::PageDescription::Row row;
        (void)row.label.assign(plain_label(text));
        if (slot != kNoSlot) {
            row.kind = app::PageDescription::RowKind::Action;
            if (sel_.v == slot) {
                row.selected = true;
                selected = static_cast<std::uint16_t>(all.size());
            }
        } else if (r + 1 == rows) {
            row.kind = app::PageDescription::RowKind::Action;
            if (sel_.v == n) {
                row.selected = true;
                selected = static_cast<std::uint16_t>(all.size());
            }
        } else {
            row.kind = app::PageDescription::RowKind::Text;
        }
        all.push_back(row);
    }

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

Transition ConfigSlotsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    if ((ev.buttons & (UiEvent::Menu | UiEvent::Left | UiEvent::Back)) != 0) {
        return Transition{Transition::Kind::Pop};
    }
    if ((ev.buttons & UiEvent::Select) == 0)
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
