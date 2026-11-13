// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/config_slots_page.h"

#include <array>
#include <span>
#include <string>
#include <utility>

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

constexpr proto::OsdRow kStartupHeaderRow{1};
constexpr proto::OsdRow kSlotZeroRow{2};
constexpr proto::OsdRow kOtherHeaderRow{4};
constexpr proto::OsdRow kOtherFirstRow{5};

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

RowCount ConfigSlotsPage::slot_count() const noexcept {
    return table_ != nullptr ? RowCount{table_->count} : RowCount{};
}

bool ConfigSlotsPage::selectable(cores::ConfigSlot slot) const noexcept {
    if (slot.v >= slot_count().v)
        return false;
    if (mode_ == Mode::Save || slot.v == 0)
        return true;
    return table_->exists.has(slot);
}

SlotChoice ConfigSlotsPage::choice_of(RowIndex at) const noexcept {
    if (at.v >= slot_count().v)
        return SlotChoice{};
    return SlotChoice{config_slot_of(at)};
}

void ConfigSlotsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    asks_ = ctx.ui_requests;
    MenuMask m{};
    for (const RowIndex i : rows(slot_count())) {
        if (selectable(config_slot_of(i)))
            m = m.with(i);
    }
    mask_ = m.with(row_after(slot_count()));
}

void ConfigSlotsPage::ask_slot(cores::ConfigSlot slot) {
    const bool landed = mode_ == Mode::Load
        ? push_ask(asks_, app::UiRequest::LoadCoreConfig{.slot = slot, .scope = {drawn_}})
        : push_ask(asks_, app::UiRequest::SaveCoreConfig{.slot = slot, .scope = {drawn_}});
    if (!landed) {
        refusals_.add();
        return;
    }
    ++published_;
}

std::optional<cores::ConfigSlot> ConfigSlotsPage::slot_at(OsdRow row) const noexcept {
    const RowCount rows = osd_height();
    if (!osd_rows_of(rows).covers(row) || row == last_osd_row(rows))
        return std::nullopt;
    if (row == kSlotZeroRow)
        return cores::ConfigSlot{0};
    if (row < kOtherFirstRow)
        return std::nullopt;

    const RowCount at = row_index_of(row) - row_index_of(kOtherFirstRow) + kOneRow;
    if (at.v >= slot_count().v)
        return std::nullopt;
    return config_slot_of(row_after(at));
}

bool ConfigSlotsPage::row_stippled(OsdRow at) const noexcept {
    const std::optional<cores::ConfigSlot> slot = slot_at(at);
    return slot.has_value() && !selectable(*slot);
}

std::string ConfigSlotsPage::row_text(OsdRow at) const {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    const RowCount rows = osd_height();
    if (rows.v == 0)
        return {};
    if (at == kStartupHeaderRow)
        return kStartupHeader;
    if (at == kOtherHeaderRow)
        return kOtherHeader;
    if (at == last_osd_row(rows))
        return kStdBack;
    const std::optional<cores::ConfigSlot> slot = slot_at(at);
    if (!slot)
        return {};

    if (!selectable(*slot))
        return "  ";
    return std::string{"  "} + std::string(table_->preview[slot->v].view());
}

bool ConfigSlotsPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    (void)out.page.title.assign(mode_ == Mode::Load ? "Load Config" : "Save Config");
    (void)out.page.footer.assign(mode_ == Mode::Load ? "A Load" : "A Save");
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowCount height = osd_height();
    const SlotChoice sel = choice_of(sel_);

    struct Drawn {
        OsdRow row;
        std::string text;
        std::optional<cores::ConfigSlot> slot;
        bool is_back = false;
    };

    std::array<Drawn, kMaxPageRows> storage{};
    const RowSpan<Drawn> drawn_out{std::span<Drawn>{storage}};
    RowCount drawn_n{};
    RowIndex selected{};
    for (const RowIndex r : rows(height)) {
        const OsdRow at = osd_row_at(r.before());
        std::string text = row_text(at);
        if (blank_text(text))
            continue;
        Drawn& d = drawn_out[row_after(drawn_n)];
        d.row = at;
        d.text = std::move(text);
        d.slot = slot_at(at);
        d.is_back = at == last_osd_row(height);
        if (d.slot && d.slot == sel.slot)
            selected = row_after(drawn_n);
        if (d.is_back && sel.back())
            selected = row_after(drawn_n);
        drawn_n += kOneRow;
    }

    const app::HdWindow win = out.window(selected, drawn_n);
    const RowSpan<const Drawn> drawn{std::span<const Drawn>{storage}};

    const auto inverted = [&](OsdRow row) {
        const std::optional<cores::ConfigSlot> slot = slot_at(row);
        if (slot)
            return slot == sel.slot;
        return row == last_osd_row(height) && sel.back();
    };

    const RowRange window = rows(win.first, win.count);
    for (const RowIndex at : window) {
        const Drawn& d = drawn[at];
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        (void)r.hd.label.assign(plain_label(d.text));
        if (d.slot) {
            r.hd.kind = app::PageDescription::RowKind::Action;
            r.hd.selected = d.slot == sel.slot;
        } else if (d.is_back) {
            r.hd.kind = app::PageDescription::RowKind::Action;
            r.hd.selected = sel.back();
        } else {
            r.hd.kind = app::PageDescription::RowKind::Text;
        }
        r.classic = classic_row(
            d.row,
            d.text,
            {.inverted = inverted(d.row),
             .stippled = row_stippled(d.row),
             .arrow = OsdRenderer::kArrowNone}
        );
    }

    const RowRange drawn_rows = rows(drawn_n);
    RowIndex di{};
    for (const RowIndex r : rows(height)) {
        const OsdRow at = osd_row_at(r.before());
        const bool drawn_here = drawn_rows.contains(di) && drawn[di].row == at;
        if (drawn_here && window.contains(di)) {
            di = di.next();
            continue;
        }
        if (drawn_here)
            di = di.next();
        out.rows[out.count++].classic = classic_row(
            at,
            row_text(at),
            {.inverted = inverted(at),
             .stippled = row_stippled(at),
             .arrow = OsdRenderer::kArrowNone}
        );
    }
    return true;
}

Transition ConfigSlotsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ConfigSlotsPage);
    if (ev.buttons.has_any({UiEvent::Menu | UiEvent::Left | UiEvent::Back})) {
        return Transition::pop();
    }
    if (!ev.buttons.has_any({UiEvent::Select}))
        return Transition{};

    const SlotChoice sel = choice_of(sel_);
    if (sel.back())
        return Transition::pop();
    if (!selectable(*sel.slot)) {
        refusals_.add();
        return Transition{};
    }
    ask_slot(*sel.slot);
    return Transition::close_all();
}

}
