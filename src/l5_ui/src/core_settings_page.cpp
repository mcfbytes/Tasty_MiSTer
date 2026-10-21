// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/core_settings_page.h"

#include <string>
#include <utility>

#include "app/hd_window.h"
#include "ui/arc_label.h"
#include "ui/browser_launch.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr const char kStdExit[] = "            exit";
constexpr const char kStdBack[] = "            back";
constexpr char kSubmenuArrow = '\x16';

constexpr std::size_t kArrowColumn = 27;

constexpr std::size_t kFileRowWidth = 28;

std::string_view plain_label(std::string_view t) noexcept {
    while (!t.empty() && t.front() == ' ')
        t.remove_prefix(1);
    while (!t.empty() && (t.back() == ' ' || t.back() == kSubmenuArrow))
        t.remove_suffix(1);
    return t;
}

app::PageDescription::RowKind row_kind_of(cores::OptionRow::Kind k) noexcept {
    switch (k) {
        case cores::OptionRow::Kind::Separator:
            return app::PageDescription::RowKind::Separator;
        case cores::OptionRow::Kind::Link:
            return app::PageDescription::RowKind::Submenu;
        case cores::OptionRow::Kind::Cycle:
            return app::PageDescription::RowKind::Value;
        case cores::OptionRow::Kind::Action:
        case cores::OptionRow::Kind::File:
        case cores::OptionRow::Kind::Host:
        case cores::OptionRow::Kind::kCount:
            break;
    }
    return app::PageDescription::RowKind::Action;
}

}

CoreSettingsPage::CoreSettingsPage(Spec s)
    : table_(s.table),
      page_(s.page),
      title_(std::move(s.title)),
      mt32_available_(s.mt32_available),
      joyswap_(s.joyswap),
      arc_{s.custom_aspect_ratio[0], s.custom_aspect_ratio[1]},
      conf_gen_(s.conf_gen) {
    rebuild();
}

std::string CoreSettingsPage::value_label(const cores::OptionRow& d, std::uint32_t slot) const {
    if (slot >= d.values.size())
        return {};
    return resolve_arc(d.values[slot], arc_);
}

std::string CoreSettingsPage::cycle_value(const app::OptionTable::Row& r) const {
    const cores::OptionRow& d = *r.decl;
    const std::string resolved = resolve_arc(r.text.view(), arc_);
    return resolved.empty() ? value_label(d, drawn_choice(r)) : resolved;
}

std::uint32_t CoreSettingsPage::drawn_choice(const app::OptionTable::Row& r) const {
    const cores::OptionRow& d = *r.decl;
    const std::uint32_t n = d.choice_count();
    if (n == 0)
        return 0;
    const std::uint32_t cur = r.current < n ? r.current : 0;
    return value_label(d, cur).empty() ? 0 : cur;
}

void CoreSettingsPage::rebuild() {
    lines_.clear();
    selectable_ = 0;
    if (table_ == nullptr)
        return;
    const std::size_t n = table_->count;
    for (std::size_t i = 0; i < n; ++i) {
        const app::OptionTable::Row& row = table_->rows[i];
        if (!row.decl || row.decl->page != page_)
            continue;

        if (row.decl->kind == cores::OptionRow::Kind::Link
            && row.decl->link == cores::OptionRow::Link::Mt32Pi
            && !mt32_available_) {
            continue;
        }
        Line l;
        l.index = static_cast<std::uint8_t>(i);
        l.selectable = row.decl->kind != cores::OptionRow::Kind::Separator;
        if (l.selectable)
            l.ordinal = selectable_++;
        lines_.push_back(l);
    }
}

std::uint32_t CoreSettingsPage::exit_drawn() const noexcept {
    const std::uint32_t rows = osd_height().v;
    const auto content = static_cast<std::uint32_t>(lines_.size());

    return (content + 1 <= rows && rows != 0) ? rows - 1 : content;
}

const CoreSettingsPage::Line* CoreSettingsPage::line_for_ordinal(std::uint32_t o) const noexcept {
    for (const Line& l : lines_) {
        if (l.selectable && l.ordinal == o)
            return &l;
    }
    return nullptr;
}

void CoreSettingsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(CoreSettingsPage);
    asks_ = ctx.ui_requests;
    link_tx_ = ctx.link_tx;
    browser_ = ctx.launches.browser;
    recents_ = ctx.recents;
    core_dir_ = ctx.sys.core_dir;
}

void CoreSettingsPage::ask_mount(std::uint8_t slot, std::string_view path) {
    const proto::IoIndex index{slot};
    const app::CoreScope scope{conf_gen_};
    const bool landed = path.empty()
        ? push_ask(asks_, app::UiRequest::UnmountImage{.index = index, .scope = scope})
        : push_ask(asks_, app::UiRequest::MountImage{.index = index, .scope = scope}, path);
    if (!landed) {
        ++refusals_;
        return;
    }
    ++published_;
}

template <class A>
void CoreSettingsPage::push_op(const A& op) {
    TASTY_SEAT_BODY(CoreSettingsPage);
    if (!link_tx_ || !link_tx_->push(op)) {
        ++refusals_;
        return;
    }
    ++published_;
}

MenuMask CoreSettingsPage::mask() const {
    TASTY_SEAT_BODY(CoreSettingsPage);
    const std::uint64_t n = static_cast<std::uint64_t>(selectable_) + 1;
    if (n >= 64)
        return MenuMask{~std::uint64_t{0}};
    return MenuMask{(std::uint64_t{1} << n) - 1};
}

std::string CoreSettingsPage::compose(const Line& l) const {
    const auto& tr = table_->rows[l.index];
    const cores::OptionRow& d = *tr.decl;
    std::string s;
    switch (d.kind) {
        case cores::OptionRow::Kind::Separator:
        case cores::OptionRow::Kind::kCount:
            return s;
        case cores::OptionRow::Kind::Action:
            s.assign(d.label);
            return s;
        case cores::OptionRow::Kind::Link:
            s.assign(d.label);
            s.resize(kArrowColumn, ' ');
            s.push_back(kSubmenuArrow);
            return s;
        case cores::OptionRow::Kind::Host:
            s.assign(d.label);

            if (d.values.size() >= 2)
                s.append(d.values[joyswap_ ? 1 : 0]);
            return s;
        case cores::OptionRow::Kind::Cycle: {
            s.assign(d.label);
            s.append(cycle_value(tr));
            return s;
        }
        case cores::OptionRow::Kind::File:
            s.assign(d.label);
            s.append(tr.text.view());
            if (s.size() > kFileRowWidth)
                s.resize(kFileRowWidth);
            return s;
    }
    return s;
}

std::string CoreSettingsPage::row_text(std::uint32_t drawn) const {
    TASTY_SEAT_BODY(CoreSettingsPage);
    if (drawn < lines_.size())
        return compose(lines_[drawn]);
    if (drawn == exit_drawn())
        return page_ == 0 ? kStdExit : kStdBack;
    return {};
}

template <class Visit>
void CoreSettingsPage::for_each_row(Visit&& visit) const {
    const std::uint32_t exit_row = exit_drawn();
    const auto content = static_cast<std::uint32_t>(lines_.size());
    const std::uint32_t last = content > exit_row + 1 ? content : exit_row + 1;
    for (std::uint32_t drawn = 0; drawn < last; ++drawn) {
        const Line* line = drawn < content ? &lines_[drawn] : nullptr;
        bool selected = false;
        bool stippled = false;
        if (line != nullptr) {
            selected = line->selectable && sel_.v == line->ordinal;
            stippled = table_->rows[line->index].dimmed;
        } else if (drawn == exit_row) {
            selected = sel_.v == selectable_;
        }
        const std::string text = row_text(drawn);
        visit(drawn, line, std::string_view{text}, selected, stippled);
    }
}

void CoreSettingsPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(CoreSettingsPage);
    out.set_title(title_);
    const std::uint32_t rows = osd_height().v;
    if (rows == 0)
        return;
    const bool at_root = page_ == 0;
    const std::uint32_t exit_row = exit_drawn();
    const bool scrolls = exit_row + 1 > rows;

    std::vector<std::string> composed(rows);
    std::vector<std::uint8_t> marks(rows);
    for_each_row(
        [&](std::uint32_t drawn, const Line*, std::string_view text, bool selected, bool stippled) {
            if (drawn < first_row_ || drawn - first_row_ >= rows)
                return;
            const std::uint32_t r = drawn - first_row_;
            composed[r].assign(text);
            marks[r] = static_cast<std::uint8_t>((selected ? 1u : 0u) | (stippled ? 2u : 0u));
        }
    );

    for (std::uint32_t r = 0; r < rows; ++r) {
        const std::uint32_t drawn = r + first_row_;

        const bool ring = at_root && drawn == exit_row;
        out.set_arrow(
            ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight) : OsdRenderer::kArrowNone
        );

        std::uint8_t leftchar = 0;
        if (scrolls) {
            if (r == 0 && first_row_ != 0) {
                leftchar = 17;
            } else if (r + 1 == rows && !ring) {
                leftchar = 16;
            }
        }
        out.text(
            OsdRow{static_cast<std::uint8_t>(r)},
            composed[r],
            (marks[r] & 1u) != 0,
            (marks[r] & 2u) != 0,
            leftchar
        );
    }
}

bool CoreSettingsPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(CoreSettingsPage);
    const std::uint16_t hint = out.first;
    out = {};
    (void)out.title.assign(title_);

    const std::uint32_t exit_row = exit_drawn();
    std::vector<app::PageDescription::Row> all;
    std::uint16_t selected = 0;
    for_each_row(
        [&](std::uint32_t drawn, const Line* line, std::string_view text, bool sel, bool stippled) {
            if (line == nullptr && drawn != exit_row)
                return;
            app::PageDescription::Row row;
            row.enabled = !stippled;
            if (line == nullptr) {
                row.kind = app::PageDescription::RowKind::Action;
                (void)row.label.assign(plain_label(text));
            } else {
                const app::OptionTable::Row& tr = table_->rows[line->index];
                const cores::OptionRow& d = *tr.decl;
                row.kind = row_kind_of(d.kind);
                (void)row.label.assign(plain_label(d.label));
                if (d.kind == cores::OptionRow::Kind::Cycle) {
                    row.steps = true;
                    (void)row.value.assign(cycle_value(tr));
                }
            }
            if (sel) {
                row.selected = true;
                selected = static_cast<std::uint16_t>(all.size());
            }
            all.push_back(row);
        }
    );

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

void CoreSettingsPage::track_viewport() noexcept {
    TASTY_SEAT_BODY(CoreSettingsPage);
    const std::uint32_t rows = osd_height().v;
    if (rows == 0)
        return;
    const std::uint32_t exit_row = exit_drawn();
    if (exit_row + 1 <= rows) {
        first_row_ = 0;
        return;
    }
    if (sel_.v == 0)
        first_row_ = 0;
    std::uint32_t drawn = exit_row;
    if (sel_.v != selectable_) {
        const Line* l = line_for_ordinal(sel_.v);
        if (l == nullptr)
            return;
        drawn = static_cast<std::uint32_t>(l - lines_.data());
    }
    if (drawn < first_row_) {
        first_row_ = drawn;
    } else if (drawn >= first_row_ + rows) {
        first_row_ = drawn - rows + 1;
    }
}

Transition CoreSettingsPage::leave() {
    TASTY_SEAT_BODY(CoreSettingsPage);
    if (armed_) {
        ask_reset(proto::ResetEdge::option_settle());
        armed_ = false;
    }
    return Transition{Transition::Kind::Pop};
}

void CoreSettingsPage::ask_reset(proto::ResetEdge edge) {
    const app::UiRequest::ResetCore req{.edge = edge, .scope = app::CoreScope{conf_gen_}};
    if (!push_ask(asks_, req)) {
        ++refusals_;
        return;
    }
    ++published_;
}

BrowserSession::Request CoreSettingsPage::browser_request(const app::OptionTable::Row& r) {
    BrowserSession::Request req;
    req.extensions = std::string(r.ext.view());
    req.scan.include_dirs = true;
    req.policy.eject_on_backspace = r.decl->browse_unmount;
    req.games_dir = core_dir_;
    req.recents_core = core_dir_;
    req.recents = app::RecentsStore::ListId{
        app::RecentsStore::ListId::Kind::Mount,
        static_cast<std::uint8_t>(r.decl->slot.v)
    };
    req.host = infra::opt_ref_of(static_cast<IBrowserHost*>(this));
    pending_slot_ = static_cast<std::uint8_t>(r.decl->slot.v);
    return req;
}

Transition CoreSettingsPage::open_browser(const app::OptionTable::Row& r, bool recents_first) {
    TASTY_SEAT_BODY(CoreSettingsPage);
    if (!browser_) {
        ++refusals_;
        return Transition{};
    }
    BrowserSession::Request req = browser_request(r);
    if (recents_first) {

        if (!recents_) {
            ++refusals_;
            return Transition{};
        }
        if (recents_->load(req.recents_core, *req.recents).empty())
            return Transition{};
        req.recents_first = true;
    }
    browser_->pending = std::move(req);
    return Transition{Transition::Kind::Push, PageId::Browser};
}

Transition CoreSettingsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(CoreSettingsPage);
    const bool at_root = page_ == 0;

    if ((ev.buttons & UiEvent::Menu) != 0)
        return Transition{Transition::Kind::CloseAll};

    const bool select = (ev.buttons & UiEvent::Select) != 0;
    const bool plus = (ev.buttons & UiEvent::Plus) != 0;
    const bool minus = (ev.buttons & UiEvent::Minus) != 0;
    const bool back = (ev.buttons & UiEvent::Back) != 0;
    const bool left = (ev.buttons & UiEvent::Left) != 0;
    const bool right = (ev.buttons & UiEvent::Right) != 0;
    const bool recent = (ev.buttons & UiEvent::Recent) != 0;

    if (!at_root && (back || left))
        return leave();
    if (at_root && !select && !plus && !minus && !recent) {

        if (right)
            return Transition{Transition::Kind::Replace, PageId::System};
        if (left)
            return Transition{Transition::Kind::Replace, PageId::Misc, ItemId{3}};
        return Transition{};
    }
    if (!select && !plus && !minus && !recent)
        return Transition{};

    if (sel_.v == selectable_) {
        if (!select)
            return Transition{};
        return at_root ? Transition{Transition::Kind::Leave} : leave();
    }
    const Line* l = line_for_ordinal(sel_.v);
    if (l == nullptr || table_ == nullptr || !table_->rows[l->index].decl) {
        ++refusals_;
        return Transition{};
    }
    const app::OptionTable::Row& tr = table_->rows[l->index];
    const cores::OptionRow& d = *tr.decl;

    switch (d.kind) {
        case cores::OptionRow::Kind::File: {

            if (!select && !recent) {
                ++refusals_;
                return Transition{};
            }

            if (tr.dimmed) {
                ++refusals_;
                return Transition{};
            }
            if (d.ejects_on_select && tr.current != 0) {
                ask_mount(static_cast<std::uint8_t>(d.slot.v), {});
                return Transition{};
            }
            return open_browser(tr, !select && recent);
        }
        case cores::OptionRow::Kind::Cycle: {
            const std::uint32_t n = d.choice_count();
            if (n == 0) {
                ++refusals_;
                return Transition{};
            }

            const std::uint32_t raw = tr.current;
            const std::uint32_t cur = value_label(d, raw).empty() ? 0 : raw;
            std::uint32_t next = minus ? (cur == 0 ? n - 1 : cur - 1)
                                       : (cur + 1 > n - 1 ? 0 : cur + 1);

            if (!minus) {
                if (value_label(d, next).empty())
                    next = 0;
            } else {
                for (std::uint32_t i = 0; i < n && value_label(d, next).empty(); ++i) {
                    next = (next + n - 1) % n;
                }
                if (value_label(d, next).empty())
                    next = cur;
            }
            const auto choice = static_cast<std::uint8_t>(next);
            push_op(proto::LinkOp::SetCoreOption{.row = l->index, .choice = choice});
            if (d.arms_reset)
                armed_ = true;
            return Transition{};
        }
        case cores::OptionRow::Kind::Action: {
            if (!select)
                return Transition{};
            if (d.reset_act == cores::OptionRow::ResetAct::None) {
                ++refusals_;
                return Transition{};
            }

            ask_reset(
                proto::ResetEdge::option_action(d.reset_act == cores::OptionRow::ResetAct::Cold)
            );
            return Transition{Transition::Kind::CloseAll};
        }
        case cores::OptionRow::Kind::Link:
            if (!select)
                return Transition{};
            switch (d.link) {
                case cores::OptionRow::Link::SubPage:
                    return Transition{
                        Transition::Kind::Push,
                        PageId::CoreSettings,
                        ItemId{d.link_page}
                    };
                case cores::OptionRow::Link::LoadConfig:
                    return Transition{Transition::Kind::Push, PageId::ConfigSlots, ItemId{0}};
                case cores::OptionRow::Link::SaveConfig:
                    return Transition{Transition::Kind::Push, PageId::ConfigSlots, ItemId{1}};
                case cores::OptionRow::Link::Mt32Pi:
                    return Transition{Transition::Kind::Push, PageId::Mt32Pi};
                case cores::OptionRow::Link::None:
                case cores::OptionRow::Link::kCount:
                    break;
            }
            ++refusals_;
            return Transition{};
        case cores::OptionRow::Kind::Host:
        case cores::OptionRow::Kind::Separator:
        case cores::OptionRow::Kind::kCount:
            break;
    }
    ++refusals_;
    return Transition{};
}

Transition CoreSettingsPage::on_picked(const BrowserEntry& e, std::string_view path) {
    TASTY_SEAT_BODY(CoreSettingsPage);
    if (path.empty()) {
        ++refusals_;
        return Transition{Transition::Kind::Pop};
    }
    const app::RecentsStore::ListId id{app::RecentsStore::ListId::Kind::Mount, pending_slot_};
    if (recents_ && !recents_->update(core_dir_, id, path, e.altname))
        ++refusals_;
    ask_mount(pending_slot_, path);
    return Transition{Transition::Kind::Pop};
}

Transition CoreSettingsPage::on_cancel() {
    TASTY_SEAT_BODY(CoreSettingsPage);
    return Transition{Transition::Kind::Pop};
}

Transition CoreSettingsPage::on_eject() {
    TASTY_SEAT_BODY(CoreSettingsPage);
    ask_mount(pending_slot_, {});
    return Transition{Transition::Kind::Pop};
}

}
