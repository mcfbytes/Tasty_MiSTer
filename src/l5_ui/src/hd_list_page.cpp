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
#include "ui/draw_classic.h"
#include "ui/hd_focus_memory.h"
#include "ui/hd_rewalk.h"
#include "ui/osd_renderer.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/ui_event.h"

namespace mister::ui {

using RowKind = app::PageDescription::RowKind;

HdListPage::HdListPage(std::unique_ptr<IHdPlan> plan, ItemId push_focus, HdFocusMemory& memory)
    : plan_(std::move(plan)),
      push_focus_(push_focus),
      memory_(memory) {
    TASTY_SEAT_BODY(HdListPage);
    std::optional<RowIndex> start;
    if (plan_->remembers_focus())
        start = recalled_();
    if (!start)
        start = first_focusable_from_(RowIndex{});
    if (start)
        sel_ = *start;
}

PageId HdListPage::id() const {
    TASTY_SEAT_BODY(HdListPage);
    return plan_->id();
}

HdListPage::PageRows HdListPage::page_rows_() const {
    const RowCount n = RowCount::of(plan_->rows().size());
    PageRows s{};
    s.pinned = std::min({plan_->pinned(), n, kMaxRows});
    const RowCount body = n - s.pinned;
    s.body = std::min(body, kMaxRows - s.pinned);
    s.dropped = body - s.body;
    return s;
}

IHdRow& HdListPage::row_(const PageRows& s, RowIndex page_row) const {
    const RowSpan<const std::reference_wrapper<IHdRow>> plan_rows{plan_->rows()};
    const RowIndex at = page_row < row_after(s.body)
        ? page_row
        : row_after(plan_rows.size() - s.pinned) + (page_row - row_after(s.body));
    return plan_rows[at].get();
}

bool HdListPage::shown_(const PageRows& s, RowIndex page_row) const {
    return page_row < s.end() && row_(s, page_row).shown();
}

bool HdListPage::focusable_(RowIndex page_row) const {
    const PageRows s = page_rows_();
    return shown_(s, page_row) && focusable(row_(s, page_row).describe());
}

std::optional<RowIndex> HdListPage::first_focusable_from_(RowIndex from) const {
    const PageRows s = page_rows_();
    const RowCount n = s.addressed();
    if (n.empty())
        return std::nullopt;
    RowIndex at = from;
    while (at >= s.end())
        at -= n;
    for (RowCount left = n; !left.empty(); left -= kOneRow) {
        if (focusable_(at))
            return at;
        at += kOneRow;
        if (at >= s.end())
            at = RowIndex{};
    }
    return std::nullopt;
}

std::optional<RowIndex> HdListPage::recalled_() const {
    const std::optional<HdFocusMemory::Focus> f = memory_.recall(
        FocusKey{plan_->id(), push_focus_}
    );
    if (!f)
        return std::nullopt;
    const PageRows s = page_rows_();
    const RowCount n = s.addressed();
    if (f->key) {
        for (const RowIndex i : rows(n)) {
            if (focusable_(i) && row_(s, i).key() == f->key)
                return i;
        }
    }
    const RowIndex at = f->row;
    for (RowCount d{}; d <= std::max(at.before(), n); d += kOneRow) {
        if (at.before() >= d && focusable_(at - d))
            return at - d;
        if (focusable_(at + d))
            return at + d;
    }
    return std::nullopt;
}

HdListPage::Window HdListPage::window_(RowIndex prev_first) const {
    const PageRows s = page_rows_();
    Window w{};
    RowSet<kMaxRows.size()> body{};
    RowIndex sel_at{};
    bool sel_in_body = false;
    for (const RowIndex i : rows(s.body)) {
        if (!shown_(s, i))
            continue;
        if (i == sel_) {
            sel_at = row_after(body.n);
            sel_in_body = true;
        }
        body.add(i);
    }
    RowSet<app::PageDescription::kMaxRows> pinned{};
    for (const RowIndex i : rows(row_after(s.body), s.pinned)) {
        if (pinned.n >= RowCount::of(pinned.at.size()))
            break;
        if (shown_(s, i))
            pinned.add(i);
    }
    w.pinned = pinned.n;
    const RowCount height = RowCount::of(app::PageDescription::kMaxRows) - w.pinned;

    const RowIndex selected = sel_in_body ? sel_at : prev_first;
    const app::HdWindow bw = app::hd_window(selected, body.n, prev_first, height);
    w.first = bw.first;
    w.total = body.n + w.pinned;
    for (const RowIndex k : rows(w.first, bw.count)) {
        w.page_row[row_after(w.count)] = body[k];
        w.count += kOneRow;
    }
    for (const RowIndex k : rows(w.pinned)) {
        w.page_row[row_after(w.count)] = pinned[k];
        w.count += kOneRow;
    }
    return w;
}

void HdListPage::focus_(RowIndex i) {
    sel_ = i;
    if (!plan_->remembers_focus())
        return;
    const PageRows s = page_rows_();
    const std::optional<RowKey> key = i < s.end() ? row_(s, i).key() : std::optional<RowKey>{};
    memory_.remember(FocusKey{plan_->id(), push_focus_}, HdFocusMemory::Focus{i, key});
}

MenuMask HdListPage::mask() const {
    TASTY_SEAT_BODY(HdListPage);
    MenuMask m{};
    const PageRows s = page_rows_();
    for (const RowIndex i : rows(s.addressed())) {
        if (focusable_(i))
            m = m.with(i);
    }
    return m;
}

void HdListPage::set_selection(RowIndex i) {
    TASTY_SEAT_BODY(HdListPage);
    focus_(i);
}

RowIndex HdListPage::selection() const {
    TASTY_SEAT_BODY(HdListPage);
    return sel_;
}

std::optional<Page::TierRun> HdListPage::tier_run(RowIndex at) const {
    TASTY_SEAT_BODY(HdListPage);
    const MenuMask m = mask();
    if (!m.has(at))
        return std::nullopt;
    const PageRows s = page_rows_();
    const app::RowTier tier = row_(s, at).describe().tier;
    MenuMask run = row_bit(at);
    for (RowIndex i = at + kOneRow; i < row_after(kMaxRows); i += kOneRow) {
        if (!m.has(i))
            continue;
        if (row_(s, i).describe().tier != tier)
            break;
        run = run.with(i);
    }
    for (RowIndex i = at; i > RowIndex{};) {
        i -= kOneRow;
        if (!m.has(i))
            continue;
        if (row_(s, i).describe().tier != tier)
            break;
        run = run.with(i);
    }
    return TierRun{.tier = tier, .rows = run};
}

bool HdListPage::empty() const {
    TASTY_SEAT_BODY(HdListPage);
    return !first_focusable_from_(RowIndex{}).has_value();
}

std::uint32_t HdListPage::refusals() const noexcept {
    TASTY_SEAT_BODY(HdListPage);
    return refusals_ + plan_->refusals();
}

RowCount HdListPage::rows_dropped() const noexcept {
    TASTY_SEAT_BODY(HdListPage);
    return page_rows_().dropped;
}

bool HdListPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(HdListPage);
    const Window w = window_(out.page.first);
    const PageRows s = page_rows_();
    (void)out.page.title.assign(plan_->title());
    out.title_arrow = OsdRenderer::kArrowNone;
    out.page.first = w.first;
    out.page.total = w.total;
    out.page.pinned = w.pinned;
    out.count = 0;

    for (const RowIndex k : rows(w.count)) {
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        app::PageDescription::Row d = row_(s, w.page_row[k]).describe();
        d.selected = focusable(d) && w.page_row[k] == sel_;
        r.hd = d;
    }

    const Window c = window_(RowIndex{});
    for (const RowIndex line : rows(c.count)) {
        const RowIndex page_row = c.page_row[line];
        const app::PageDescription::Row d = row_(s, page_row).describe();
        std::string text(d.label.view());
        if (d.kind == RowKind::Value && !d.value.empty())
            text.append("  ").append(d.value.view());
        if (d.kind == RowKind::Submenu)
            text.append(" >");
        const bool inverted = focusable(d) && page_row == sel_;
        out.rows[out.count++].classic = classic_row(
            osd_row_at(line.before()),
            text,
            {.inverted = inverted, .arrow = OsdRenderer::kArrowNone}
        );
    }

    (void)describe_hub(out.page);
    return true;
}

bool HdListPage::describe_hub(app::PageDescription& out) const {
    TASTY_SEAT_BODY(HdListPage);
    out.hub = {};
    if (!plan_->hub())
        return false;
    const PageRows s = page_rows_();
    for (const RowIndex i : rows(s.addressed())) {
        if (out.hub.count >= out.hub.items.size())
            break;
        if (!focusable_(i))
            continue;
        const app::PageDescription::Row d = row_(s, i).describe();
        app::PageDescription::Hub::Item& item = out.hub.items[out.hub.count];
        (void)item.label.assign(d.label.view());
        item.icon = d.icon;
        item.enabled = d.enabled;
        if (i == sel_)
            out.hub.open = out.hub.count;
        ++out.hub.count;
    }
    return out.hub.count != 0;
}

Transition HdListPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(HdListPage);
    if (ev.buttons.has_any({UiEvent::Menu}))
        return Transition{Transition::Kind::CloseAll};
    if (ev.buttons.has_any({UiEvent::Back}))
        return Transition{Transition::Kind::Leave};
    const bool select = ev.buttons.has_any({UiEvent::Select});
    const bool step = ev.buttons.has_any({UiEvent::Plus | UiEvent::Minus});
    if (!select && !step)
        return Transition{};
    if (!focusable_(sel_)) {
        if (select)
            ++refusals_;
        return Transition{};
    }
    IHdRow& row = row_(page_rows_(), sel_);
    if (select)
        return row.select();
    if (!row.describe().steps)
        return Transition{};
    return row.step(ev.buttons.has_any({UiEvent::Plus}) ? CycleDir::Next : CycleDir::Back);
}

void HdListPage::refocus_() {
    if (focusable_(sel_))
        return;
    if (const std::optional<RowIndex> next = first_focusable_from_(sel_ + kOneRow)) {
        focus_(*next);
    }
}

void HdListPage::on_facts(const SystemFacts& facts, const SessionFacts& session) {
    TASTY_SEAT_BODY(HdListPage);
    plan_->refresh(facts, session);
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

proto::SubmenuNo HdListPage::page() const noexcept {
    TASTY_SEAT_BODY(HdListPage);
    return proto::kRootSubmenu;
}

}
