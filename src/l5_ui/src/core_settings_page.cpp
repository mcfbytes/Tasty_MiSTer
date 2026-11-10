// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/core_settings_page.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "infra/indexed_array.h"
#include "infra/message_sum.h"
#include "ui/arc_label.h"
#include "ui/browser_launch.h"
#include "ui/config_slots_page.h"
#include "ui/draw_classic.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_ask.h"
#include "ui/slot_labels.h"
#include "ui/options_keys.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr const char kStdExit[] = "            exit";
constexpr const char kStdBack[] = "            back";

std::string_view plain_label(std::string_view t) noexcept {
    while (!t.empty() && t.front() == ' ')
        t.remove_prefix(1);
    while (!t.empty() && (t.back() == ' ' || t.back() == glyph_char(Glyph::SubmenuArrow)))
        t.remove_suffix(1);
    return t;
}

class ResolvedLabels final : public ISlotLabels {
public:
    ResolvedLabels(const cores::OptionRow::CycleArm& d, const std::string_view (&arc)[2]) noexcept
        : cycle_(&d),
          arc_(&arc) {}

    bool has_label(proto::FieldValue v) const noexcept override {
        return !resolve_arc(cycle_->value_name(v), *arc_).empty();
    }

private:
    const cores::OptionRow::CycleArm* cycle_;
    const std::string_view (*arc_)[2];
};

struct LinkTarget {
    cores::OptionRow::Link kind;
    PageId page;

    bool takes_row_argument;
    ItemId item;
    std::string_view evidence;
};

constexpr auto kLinkTargets = std::to_array<LinkTarget>({
    {cores::OptionRow::Link::None,
     PageId::None,
     false,
     kNoItem,
     "no factory in this tree: refused and counted"},
    {cores::OptionRow::Link::SubPage,
     PageId::CoreSettings,
     true,
     kNoItem,
     "menu.cpp:4970-4985 — another page of this same table"},
    {cores::OptionRow::Link::LoadConfig,
     PageId::ConfigSlots,
     false,
     config_slots_item(ConfigSlotsPage::Mode::Load),
     "menu.cpp:4987-4999"},
    {cores::OptionRow::Link::SaveConfig,
     PageId::ConfigSlots,
     false,
     config_slots_item(ConfigSlotsPage::Mode::Save),
     "menu.cpp:5001-5013"},
    {cores::OptionRow::Link::Mt32Pi, PageId::Mt32Pi, false, kNoItem, "menu.cpp:5015-5030"},
});

static_assert(
    infra::rows_are_ordinal(kLinkTargets),
    "every Link names its target; a row without one would refuse silently"
);

constexpr infra::OptRef<const LinkTarget> link_target_of(cores::OptionRow::Link link) {
    for (const LinkTarget& t : kLinkTargets) {
        if (t.kind == link)
            return infra::OptRef<const LinkTarget>{t};
    }
    return {};
}

inline constexpr std::size_t kHostOffSlot = 0;
inline constexpr std::size_t kHostOnSlot = 1;

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

std::string CoreSettingsPage::value_label(
    const cores::OptionRow::CycleArm& d,
    proto::FieldValue slot
) const {
    return resolve_arc(d.value_name(slot), arc_);
}

std::string CoreSettingsPage::cycle_value(const app::OptionTable::Row& r) const {
    const cores::OptionRow::CycleArm* d = r.decl->cycle_arm();
    if (d == nullptr)
        return {};
    const std::string resolved = resolve_arc(r.text.view(), arc_);
    return resolved.empty() ? value_label(*d, drawn_choice(r)) : resolved;
}

proto::FieldValue CoreSettingsPage::drawn_choice(const app::OptionTable::Row& r) const {
    const cores::OptionRow::CycleArm* d = r.decl->cycle_arm();
    if (d == nullptr)
        return {};
    const proto::FieldCount choices = d->choice_count();
    if (choices == proto::FieldCount{})
        return {};

    return choice_slot(ResolvedLabels{*d, arc_}, proto::FieldValue{r.current}, choices);
}

void CoreSettingsPage::rebuild() {
    lines_.clear();
    selectable_ = RowCount{};
    const std::size_t n = table_.count;
    for (std::size_t i = 0; i < n; ++i) {
        const app::OptionTable::Row& row = table_.rows[i];
        if (!row.decl || row.decl->page != page_)
            continue;

        const cores::OptionRow::LinkArm* link = row.decl->link_arm();
        if (link != nullptr && link->link == cores::OptionRow::Link::Mt32Pi && !mt32_available_) {
            continue;
        }
        Line l;
        l.index = static_cast<std::uint8_t>(i);
        l.selectable = row.decl->kind() != cores::OptionRow::Kind::Separator;
        if (l.selectable) {
            l.ordinal = row_after(selectable_);
            selectable_ += kOneRow;
        }
        lines_.push_back(l);
    }
}

RowIndex CoreSettingsPage::exit_drawn() const noexcept {
    const RowCount rows = osd_height();
    const RowCount content = RowCount::of(lines_.size());

    return (content + kOneRow <= rows) ? row_after(rows - kOneRow) : row_after(content);
}

infra::OptRef<const CoreSettingsPage::Line> CoreSettingsPage::line_for_ordinal(
    RowIndex o
) const noexcept {
    for (const Line& l : lines_) {
        if (l.selectable && l.ordinal == o)
            return infra::OptRef<const CoreSettingsPage::Line>{l};
    }
    return {};
}

void CoreSettingsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(CoreSettingsPage);
    asks_ = ctx.ui_requests;
    link_tx_ = ctx.link_tx;
    browser_ = ctx.launches.browser;
    recents_ = ctx.recents;
    core_dir_.assign(ctx.session.core_dir.view());
}

void CoreSettingsPage::ask_mount(proto::IoIndex index, std::string_view path) {
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
    return dense_mask(selectable_ + kOneRow);
}

std::string CoreSettingsPage::compose(const Line& l) const {
    const auto& tr = table_.rows[l.index];
    const cores::OptionRow& d = *tr.decl;
    std::string s;
    switch (d.kind()) {
        case cores::OptionRow::Kind::Separator:
        case cores::OptionRow::Kind::kCount:
            return s;
        case cores::OptionRow::Kind::Action:
            s.assign(d.label);
            return s;
        case cores::OptionRow::Kind::Link:
            s.assign(d.label);
            s.resize(kArrowColumn, ' ');
            s.push_back(glyph_char(Glyph::SubmenuArrow));
            return s;
        case cores::OptionRow::Kind::Host: {
            s.assign(d.label);

            const cores::OptionRow::HostArm* host = d.host_arm();
            if (host != nullptr && host->values.size() > kHostOnSlot) {
                s.append(host->values[joyswap_ ? kHostOnSlot : kHostOffSlot]);
            }
            return s;
        }
        case cores::OptionRow::Kind::Cycle: {
            s.assign(d.label);
            s.append(cycle_value(tr));
            return s;
        }
        case cores::OptionRow::Kind::File:
            s.assign(d.label);
            s.append(tr.text.view());
            if (s.size() > kClassicRowColumns)
                s.resize(kClassicRowColumns);
            return s;
    }
    return s;
}

std::string CoreSettingsPage::row_text(RowIndex at) const {
    TASTY_SEAT_BODY(CoreSettingsPage);
    if (const infra::OptRef<const Line> line = RowSpan<const Line>{lines_}.find(at)) {
        return compose(*line);
    }
    if (at == exit_drawn())
        return page_ == proto::kRootSubmenu ? kStdExit : kStdBack;
    return {};
}

template <class Visit>
void CoreSettingsPage::for_each_row(Visit&& visit) const {
    const RowIndex exit_row = exit_drawn();
    const RowCount content = RowCount::of(lines_.size());
    const RowIndex last = std::max(row_after(content), exit_row + kOneRow);
    const RowSpan<const Line> lines{lines_};
    for (RowIndex drawn{}; drawn < last; drawn += kOneRow) {
        infra::OptRef<const Line> line{};
        if (drawn < row_after(content))
            line = infra::OptRef<const Line>{lines[drawn]};
        bool selected = false;
        bool stippled = false;
        if (line) {
            selected = line->selectable && sel_ == line->ordinal;
            stippled = table_.rows[line->index].dimmed;
        } else if (drawn == exit_row) {
            selected = sel_ == exit_ordinal();
        }
        const std::string text = row_text(drawn);
        visit(drawn, line, std::string_view{text}, selected, stippled);
    }
}

app::PageDescription::Row CoreSettingsPage::hd_row(
    infra::OptRef<const Line> line,
    std::string_view text,
    bool stippled
) const {
    app::PageDescription::Row row;
    row.enabled = !stippled;
    if (!line) {
        row.kind = app::PageDescription::RowKind::Action;
        (void)row.label.assign(plain_label(text));
        return row;
    }
    const app::OptionTable::Row& tr = table_.rows[line->index];
    const cores::OptionRow& d = *tr.decl;
    row.kind = row_kind_of(d.kind());
    (void)row.label.assign(plain_label(d.label));
    if (d.kind() == cores::OptionRow::Kind::Cycle) {
        row.steps = true;
        (void)row.value.assign(cycle_value(tr));
    }
    return row;
}

std::optional<Glyph> CoreSettingsPage::leftchar_of(OsdRow row, bool ring) const noexcept {
    const RowCount rows = osd_height();
    if (rows.empty() || exit_drawn() < row_after(rows))
        return std::nullopt;
    if (row == proto::OsdRow{} && first_row_ != RowIndex{})
        return Glyph::ArrowUp;
    if (row == last_osd_row(rows) && !ring)
        return Glyph::ArrowDown;
    return std::nullopt;
}

bool CoreSettingsPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(CoreSettingsPage);
    (void)out.page.title.assign(title_);
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowCount rows = osd_height();
    if (rows.empty())
        return false;
    const bool at_root = page_ == proto::kRootSubmenu;
    const RowIndex exit_row = exit_drawn();

    struct Drawn {
        RowIndex drawn;
        infra::OptRef<const Line> line;
        std::string text;
        bool selected;
        bool stippled;
        bool in_window = false;
    };

    std::vector<Drawn> drawn_rows;
    for_each_row([&](RowIndex drawn,
                     infra::OptRef<const Line> line,
                     std::string_view text,
                     bool selected,
                     bool stippled) {
        drawn_rows.push_back(Drawn{drawn, line, std::string{text}, selected, stippled});
    });

    const auto is_hd = [&](const Drawn& d) {
        return d.line.has_value() || d.drawn == exit_row;
    };
    RowCount total{};
    RowIndex selected{};
    for (const Drawn& d : drawn_rows) {
        if (!is_hd(d))
            continue;
        if (d.selected)
            selected = row_after(total);
        total += kOneRow;
    }
    const app::HdWindow win = out.window(selected, total);

    const RowRange window = ui::rows(win.first, win.count);
    RowIndex at{};
    for (std::size_t i = 0; i < drawn_rows.size(); ++i) {
        const Drawn& d = drawn_rows[i];
        if (!is_hd(d))
            continue;
        const RowIndex index = at;
        at += kOneRow;
        if (!window.contains(index))
            continue;
        drawn_rows[i].in_window = true;
        if (out.count >= out.rows.size())
            break;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd = hd_row(d.line, d.text, d.stippled);
        r.hd.selected = d.selected;
        if (d.drawn < first_row_ || d.drawn - first_row_ >= rows)
            continue;
        const OsdRow row = osd_row_at(d.drawn - first_row_);

        const bool ring = at_root && d.drawn == exit_row;
        r.classic = classic_row(
            row,
            d.text,
            {.inverted = d.selected,
             .stippled = d.stippled,
             .leftchar = leftchar_of(row, ring),
             .arrow = ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight)
                           : OsdRenderer::kArrowNone}
        );
    }

    for (RowCount r{}; r < rows; r += kOneRow) {
        const infra::OptRef<const Drawn> found = RowSpan<const Drawn>{drawn_rows}.find(
            first_row_ + r
        );
        if ((found && found->in_window) || out.count >= out.rows.size())
            continue;

        const bool ring = at_root && found && found->drawn == exit_row;
        const OsdRow row = osd_row_at(r);
        PageRow& pr = out.rows[out.count++];
        pr.has_hd = false;
        pr.classic = classic_row(
            row,
            found ? found->text : std::string{},
            {.inverted = found && found->selected,
             .stippled = found && found->stippled,
             .leftchar = leftchar_of(row, ring),
             .arrow = ring ? (OsdRenderer::kArrowLeft | OsdRenderer::kArrowRight)
                           : OsdRenderer::kArrowNone}
        );
    }
    return true;
}

void CoreSettingsPage::track_viewport() noexcept {
    TASTY_SEAT_BODY(CoreSettingsPage);
    const RowCount rows = osd_height();
    if (rows.empty())
        return;
    const RowIndex exit_row = exit_drawn();
    if (exit_row < row_after(rows)) {
        first_row_ = RowIndex{};
        return;
    }

    if (sel_ == RowIndex{})
        first_row_ = RowIndex{};
    RowIndex drawn = exit_row;
    if (sel_ != exit_ordinal()) {
        const infra::OptRef<const Line> l = line_for_ordinal(sel_);
        if (!l)
            return;
        drawn = RowIndex::of(&*l - lines_.data());
    }
    if (drawn < first_row_) {
        first_row_ = drawn;
    } else if (drawn >= first_row_ + rows) {
        first_row_ = drawn - rows + kOneRow;
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
    const cores::OptionRow::FileArm* file = r.decl->file_arm();
    const proto::IoIndex slot = file != nullptr ? file->slot : proto::IoIndex{};
    req.extensions = std::string(r.ext.view());
    req.scan.include_dirs = true;
    req.policy.eject_on_backspace = file != nullptr && file->browse_unmount;
    req.games_dir = core_dir_;
    req.recents_core = core_dir_;
    req.recents = app::RecentsStore::ListId{app::RecentsStore::ListId::Kind::Mount, slot};
    req.host = infra::opt_ref_of(static_cast<IBrowserHost*>(this));
    pending_slot_ = slot;
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
    const bool at_root = page_ == proto::kRootSubmenu;
    const OptionsKeys keys = OptionsKeys::of(ev);

    if (keys.menu)
        return Transition{Transition::Kind::CloseAll};

    if (!at_root && (keys.back || keys.left))
        return leave();
    if (at_root && keys.no_row_key()) {

        if (const std::optional<Transition> ring = ring_transition(at_root, keys))
            return *ring;
        return Transition{};
    }
    if (keys.no_row_key())
        return Transition{};
    const bool select = keys.select;
    const bool minus = keys.minus;
    const bool recent = keys.recent;

    if (sel_ == exit_ordinal()) {
        if (!select)
            return Transition{};
        return at_root ? Transition{Transition::Kind::Leave} : leave();
    }
    const infra::OptRef<const Line> l = line_for_ordinal(sel_);
    if (!l || !table_.rows[l->index].decl) {
        ++refusals_;
        return Transition{};
    }
    const app::OptionTable::Row& tr = table_.rows[l->index];
    const cores::OptionRow& d = *tr.decl;

    switch (d.kind()) {
        case cores::OptionRow::Kind::File: {

            if (!select && !recent) {
                ++refusals_;
                return Transition{};
            }

            if (tr.dimmed) {
                ++refusals_;
                return Transition{};
            }
            const cores::OptionRow::FileArm* file = d.file_arm();
            if (file != nullptr && file->ejects_on_select && tr.current != 0) {
                ask_mount(file->slot, {});
                return Transition{};
            }
            return open_browser(tr, !select && recent);
        }
        case cores::OptionRow::Kind::Cycle: {
            const cores::OptionRow::CycleArm* cycle = d.cycle_arm();
            if (cycle == nullptr)
                break;
            const proto::FieldCount choices = cycle->choice_count();
            if (choices == proto::FieldCount{}) {
                ++refusals_;
                return Transition{};
            }

            const ResolvedLabels labels{*cycle, arc_};

            const proto::FieldCount slots = proto::FieldCount::of(cycle->values.size());
            const proto::FieldValue cur = choice_slot(labels, proto::FieldValue{tr.current}, slots);
            proto::FieldValue next = minus ? cur.cycled_back(choices) : cur.cycled_forward(choices);

            if (!minus) {
                next = choice_slot(labels, next, slots);
            } else {
                for (proto::FieldValue i{}; choices.holds(i) && !labels.has_label(next);
                     i = i.cycled_forward(choices)) {
                    next = next.wrapped_back(choices);
                }
                if (!labels.has_label(next))
                    next = cur;
            }
            push_op(
                proto::LinkOp::SetCoreOption{.row = proto::OptionRowIndex{l->index}, .choice = next}
            );
            if (cycle->arms_reset)
                armed_ = true;
            return Transition{};
        }
        case cores::OptionRow::Kind::Action: {
            if (!select)
                return Transition{};
            const cores::OptionRow::ActionArm* action = d.action_arm();
            if (action == nullptr || action->reset_act == cores::OptionRow::ResetAct::None) {
                ++refusals_;
                return Transition{};
            }

            ask_reset(
                proto::ResetEdge::option_action(
                    action->reset_act == cores::OptionRow::ResetAct::Cold
                )
            );
            return Transition{Transition::Kind::CloseAll};
        }
        case cores::OptionRow::Kind::Link: {
            if (!select)
                return Transition{};
            const cores::OptionRow::LinkArm* link = d.link_arm();

            const infra::OptRef<const LinkTarget> found = link != nullptr
                ? link_target_of(link->link)
                : infra::OptRef<const LinkTarget>{};
            if (!found || found->page == PageId::None || link == nullptr) {
                ++refusals_;
                return Transition{};
            }
            const LinkTarget& target = *found;
            const ItemId focus = target.takes_row_argument ? item_submenu(link->link_page)
                                                           : target.item;
            return Transition{Transition::Kind::Push, target.page, focus};
        }
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
