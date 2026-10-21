// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_list_page.h"

#include <algorithm>
#include <functional>
#include <span>
#include <string>
#include <utility>

#include "app/hd_window.h"
#include "app/page_description.h"
#include "proto/types.h"
#include "ui/hd_focus_memory.h"
#include "ui/hd_rewalk.h"
#include "ui/osd_renderer.h"
#include "ui/ui_event.h"

namespace mister::ui {

using RowKind = app::PageDescription::RowKind;

HdListPage::HdListPage(std::unique_ptr<IHdPlan> plan, ItemId key, HdFocusMemory& memory)
    : plan_(std::move(plan)),
      key_(key),
      memory_(memory) {
    TASTY_SEAT_BODY(HdListPage);
    std::optional<std::size_t> start;
    if (plan_->remembers_focus())
        start = recalled_();
    if (!start)
        start = first_focusable_from_(0);
    if (start)
        sel_ = RowIndex{static_cast<std::uint32_t>(*start)};
}

PageId HdListPage::id() const {
    TASTY_SEAT_BODY(HdListPage);
    return plan_->id();
}

HdListPage::Slots HdListPage::slots_() const {
    const std::size_t n = plan_->rows().size();
    Slots s{};
    s.pinned = std::min<std::size_t>({plan_->pinned(), n, kMaxRows});
    const std::size_t body = n - s.pinned;
    s.body = std::min(body, kMaxRows - s.pinned);
    s.dropped = body - s.body;
    return s;
}

IHdRow& HdListPage::row_(const Slots& s, std::size_t slot) const {
    const std::span<const std::reference_wrapper<IHdRow>> rows = plan_->rows();
    const std::size_t at = slot < s.body ? slot : rows.size() - s.pinned + (slot - s.body);
    return rows[at].get();
}

bool HdListPage::shown_(const Slots& s, std::size_t slot) const {
    return slot < s.body + s.pinned && row_(s, slot).shown();
}

bool HdListPage::focusable_(std::size_t slot) const {
    const Slots s = slots_();
    return shown_(s, slot) && row_(s, slot).describe().kind != RowKind::Separator;
}

std::optional<std::size_t> HdListPage::first_focusable_from_(std::size_t slot) const {
    const Slots s = slots_();
    const std::size_t n = s.body + s.pinned;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t at = (slot + k) % n;
        if (focusable_(at))
            return at;
    }
    return std::nullopt;
}

std::optional<std::size_t> HdListPage::recalled_() const {
    const std::optional<HdFocusMemory::Focus> f = memory_.recall(plan_->id(), key_);
    if (!f)
        return std::nullopt;
    const Slots s = slots_();
    const std::size_t n = s.body + s.pinned;
    if (f->key) {
        for (std::size_t i = 0; i < n; ++i) {
            if (focusable_(i) && row_(s, i).key() == f->key)
                return i;
        }
    }
    const std::size_t at = f->row.v;
    for (std::size_t d = 0; d <= std::max(at, n); ++d) {
        if (at >= d && focusable_(at - d))
            return at - d;
        if (focusable_(at + d))
            return at + d;
    }
    return std::nullopt;
}

HdListPage::Window HdListPage::window_(std::uint16_t prev_first) const {
    const Slots s = slots_();
    Window w{};
    std::array<std::size_t, kMaxRows> body{};
    std::size_t body_n = 0;
    std::size_t sel_at = 0;
    bool sel_in_body = false;
    for (std::size_t i = 0; i < s.body; ++i) {
        if (!shown_(s, i))
            continue;
        if (i == sel_.v) {
            sel_at = body_n;
            sel_in_body = true;
        }
        body[body_n++] = i;
    }
    std::array<std::size_t, app::PageDescription::kMaxRows> pinned{};
    for (std::size_t i = s.body; i < s.body + s.pinned && w.pinned < pinned.size(); ++i) {
        if (shown_(s, i))
            pinned[w.pinned++] = i;
    }
    const auto height = static_cast<std::uint16_t>(app::PageDescription::kMaxRows - w.pinned);
    const auto total = static_cast<std::uint16_t>(body_n);

    const auto selected = static_cast<std::uint16_t>(sel_in_body ? sel_at : prev_first);
    const app::HdWindow bw = app::hd_window(selected, total, prev_first, height);
    w.first = bw.first;
    w.total = static_cast<std::uint16_t>(total + w.pinned);
    for (std::size_t k = 0; k < bw.count; ++k)
        w.slot[w.count++] = body[bw.first + k];
    for (std::size_t k = 0; k < w.pinned; ++k)
        w.slot[w.count++] = pinned[k];
    return w;
}

void HdListPage::focus_(RowIndex i) {
    sel_ = i;
    if (!plan_->remembers_focus())
        return;
    const Slots s = slots_();
    const std::optional<RowKey> key = i.v < s.body + s.pinned ? row_(s, i.v).key()
                                                              : std::optional<RowKey>{};
    memory_.remember(plan_->id(), key_, HdFocusMemory::Focus{i, key});
}

MenuMask HdListPage::mask() const {
    TASTY_SEAT_BODY(HdListPage);
    std::uint64_t m = 0;
    const Slots s = slots_();
    for (std::size_t i = 0; i < s.body + s.pinned; ++i) {
        if (focusable_(i))
            m |= std::uint64_t{1} << i;
    }
    return MenuMask{m};
}

void HdListPage::set_selection(RowIndex i) {
    TASTY_SEAT_BODY(HdListPage);
    focus_(i);
}

RowIndex HdListPage::selection() const {
    TASTY_SEAT_BODY(HdListPage);
    return sel_;
}

bool HdListPage::empty() const {
    TASTY_SEAT_BODY(HdListPage);
    return !first_focusable_from_(0).has_value();
}

std::uint32_t HdListPage::refusals() const noexcept {
    TASTY_SEAT_BODY(HdListPage);
    return refusals_ + plan_->refusals();
}

std::uint32_t HdListPage::rows_dropped() const noexcept {
    TASTY_SEAT_BODY(HdListPage);
    return static_cast<std::uint32_t>(slots_().dropped);
}

void HdListPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(HdListPage);
    out.set_title(plan_->title());
    out.set_arrow(OsdRenderer::kArrowNone);
    const Window w = window_(0);
    const Slots s = slots_();
    for (std::uint8_t line = 0; line < w.count; ++line) {
        const std::size_t slot = w.slot[line];
        const app::PageDescription::Row d = row_(s, slot).describe();
        std::string text(d.label.view());
        if (d.kind == RowKind::Value && !d.value.empty())
            text.append("  ").append(d.value.view());
        if (d.kind == RowKind::Submenu)
            text.append(" >");
        const bool inverted = d.kind != RowKind::Separator && slot == sel_.v;
        out.text(proto::OsdRow{line}, text, inverted);
    }
}

bool HdListPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(HdListPage);
    const Window w = window_(out.first);
    const Slots s = slots_();
    out = {};
    (void)out.title.assign(plan_->title());
    out.first = w.first;
    out.total = w.total;
    out.row_count = w.count;
    out.pinned = w.pinned;
    for (std::uint8_t k = 0; k < w.count; ++k) {
        app::PageDescription::Row d = row_(s, w.slot[k]).describe();
        d.selected = d.kind != RowKind::Separator && w.slot[k] == sel_.v;
        out.rows[k] = d;
    }
    return true;
}

Transition HdListPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(HdListPage);
    if ((ev.buttons & UiEvent::Menu) != 0)
        return Transition{Transition::Kind::CloseAll};
    if ((ev.buttons & UiEvent::Back) != 0)
        return Transition{Transition::Kind::Leave};
    const bool select = (ev.buttons & UiEvent::Select) != 0;
    const bool step = (ev.buttons & (UiEvent::Plus | UiEvent::Minus)) != 0;
    if (!select && !step)
        return Transition{};
    if (!focusable_(sel_.v)) {
        if (select)
            ++refusals_;
        return Transition{};
    }
    IHdRow& row = row_(slots_(), sel_.v);
    if (select)
        return row.select();
    if (!row.describe().steps)
        return Transition{};
    return row.step((ev.buttons & UiEvent::Plus) != 0 ? 1 : -1);
}

void HdListPage::refocus_() {
    if (focusable_(sel_.v))
        return;
    if (const std::optional<std::size_t> next = first_focusable_from_(sel_.v + 1)) {
        focus_(RowIndex{static_cast<std::uint32_t>(*next)});
    }
}

void HdListPage::on_facts(const SystemFacts& facts) {
    TASTY_SEAT_BODY(HdListPage);
    plan_->refresh(facts);
    refocus_();
}

infra::OptRef<IMaskRebuild> HdListPage::mask_rebuild() {
    TASTY_SEAT_BODY(HdListPage);
    if (!plan_->rewalker())
        return {};
    return infra::OptRef<IMaskRebuild>{static_cast<IMaskRebuild&>(*this)};
}

void HdListPage::rebuild(const proto::ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(HdListPage);
    if (const infra::OptRef<IHdRewalk> r = plan_->rewalker())
        r->rewalk(table, env);
    refocus_();
}

std::uint8_t HdListPage::page() const noexcept {
    TASTY_SEAT_BODY(HdListPage);
    return 0;
}

}
