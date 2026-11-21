// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/system_page.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

#include "app/cmd_verb_sink.h"
#include "app/hd/page_description.h"
#include "app/bitstream_resolve.h"
#include "app/ui_request_ring.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "ui/draw_classic.h"
#include "ui/free_space.h"
#include "ui/osd_renderer.h"
#include "ui/osd_tree.h"
#include "ui/page_ask.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr const char kTitle[] = "System Settings";
constexpr const char kStdExit[] = "            exit";

constexpr const char kRowCore[] = " Core                      \x16";
constexpr const char kRowRemap[] = " Remap keyboard            \x16";
constexpr const char kRowJoy[] = " Define joystick buttons   \x16";
constexpr const char kRowScripts[] = " Scripts                   \x16";
constexpr const char kRowHelp[] = " Help                      \x16";
constexpr const char kRowSave[] = " Save settings";
constexpr const char kRowReboot[] = " Reboot (hold \x16 cold reboot)";
constexpr const char kRowUseHd[] = " Use HD OSD";

constexpr std::string_view kRowAutosavePrefix = " Auto-save to SD: ";

constexpr std::string_view kHdRebootLabel = "Reboot";
constexpr std::string_view kHdRebootValue = "hold for cold reboot";

constexpr const char kRowKernelRt[] = "        kernel: RT";
constexpr const char kRowKernelStock[] = "        kernel: non-RT";

const char* kernel_row(KernelRt k) noexcept {
    switch (k) {
        case KernelRt::PreemptRt:
            return kRowKernelRt;
        case KernelRt::NotRt:
            return kRowKernelStock;
        case KernelRt::Unknown:
            break;
    }
    return "";
}

std::string_view plain_label(std::string_view t) noexcept {
    while (!t.empty() && t.front() == ' ')
        t.remove_prefix(1);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\x16'))
        t.remove_suffix(1);
    return t;
}

struct SystemRowFact {
    SystemItem item;
    proto::OsdRow line;
    std::string_view text;
    std::string_view hd_label;
    std::string_view hd_value;
    app::hd::PageDescription::RowKind kind;
};

constexpr std::array<SystemRowFact, kSystemItemTotal> kSystemRows{{
    {SystemItem::Core,
     proto::OsdRow{0},
     kRowCore,
     {},
     {},
     app::hd::PageDescription::RowKind::Submenu},
    {SystemItem::SwitchStorage,
     proto::OsdRow{5},
     {},
     {},
     {},
     app::hd::PageDescription::RowKind::Action},
    {SystemItem::RemapKeyboard,
     proto::OsdRow{7},
     kRowRemap,
     {},
     {},
     app::hd::PageDescription::RowKind::Action},
    {SystemItem::DefineJoystick,
     proto::OsdRow{8},
     kRowJoy,
     {},
     {},
     app::hd::PageDescription::RowKind::Submenu},
    {SystemItem::Scripts,
     proto::OsdRow{9},
     kRowScripts,
     {},
     {},
     app::hd::PageDescription::RowKind::Submenu},
    {SystemItem::Help,
     proto::OsdRow{10},
     kRowHelp,
     {},
     {},
     app::hd::PageDescription::RowKind::Action},
    {SystemItem::SaveSettings,
     proto::OsdRow{11},
     kRowSave,
     {},
     {},
     app::hd::PageDescription::RowKind::Submenu},
    {SystemItem::Reboot,
     proto::OsdRow{12},
     kRowReboot,
     kHdRebootLabel,
     kHdRebootValue,
     app::hd::PageDescription::RowKind::Action},
    {SystemItem::Autosave,
     proto::OsdRow{13},
     {},
     kAutosaveLabel,
     {},
     app::hd::PageDescription::RowKind::Value},
    {SystemItem::UseHd,
     proto::OsdRow{14},
     kRowUseHd,
     {},
     {},
     app::hd::PageDescription::RowKind::Action},
    {SystemItem::Exit,
     proto::OsdRow{15},
     kStdExit,
     {},
     {},
     app::hd::PageDescription::RowKind::Action},
}};

consteval bool system_rows_are_ordinal() {
    if (kSystemRows.size() != infra::ordinal(SystemItem::kCount))
        return false;
    for (std::size_t i = 0; i < kSystemRows.size(); ++i)
        if (infra::ordinal(kSystemRows[i].item) != i)
            return false;
    return true;
}

static_assert(system_rows_are_ordinal(), "kSystemRows names every SystemItem, in ordinal order");

consteval bool system_rows_ascend() {
    for (std::size_t i = 1; i < kSystemRows.size(); ++i)
        if (!(kSystemRows[i - 1].line < kSystemRows[i].line))
            return false;
    return true;
}

static_assert(system_rows_ascend(), "kSystemRows' lines ascend with its ordinals");

[[nodiscard]] constexpr const SystemRowFact* find_system_row(SystemItem it) noexcept {
    const std::size_t i = infra::ordinal(it);
    return i < kSystemRows.size() ? &kSystemRows[i] : nullptr;
}

[[nodiscard]] constexpr std::string_view system_row_label(SystemItem it) noexcept {
    const SystemRowFact* const row = find_system_row(it);
    return row ? row->text : std::string_view{};
}

app::hd::PageDescription::RowKind system_row_kind(std::optional<SystemItem> item) noexcept {
    if (!item.has_value())
        return app::hd::PageDescription::RowKind::Text;
    const SystemRowFact* const row = find_system_row(*item);

    return row ? row->kind : app::hd::PageDescription::RowKind::Action;
}

}

void SystemPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(SystemPage);
    ctx_ = infra::OptRef<PageContext>{ctx};
    route_ = ctx.route;
    ui_requests_ = ctx.ui_requests;
    scripts_.bind(ctx);
    cores_.bind(ctx);
}

namespace {

constexpr proto::OsdRow kLineVersion{1};
constexpr proto::OsdRow kLineKernel{2};
constexpr proto::OsdRow kLineStorage{3};
constexpr proto::OsdRow kLineSpace{4};
constexpr proto::OsdRow kLineGapMid{6};
constexpr std::array<proto::OsdRow, 5>
    kTextOnlyLines{kLineVersion, kLineKernel, kLineStorage, kLineSpace, kLineGapMid};

inline constexpr std::size_t kClassicPageRows = std::size(kTextOnlyLines) + kSystemRows.size();
static_assert(
    RowCount::truncating(kClassicPageRows) == kFullOsdRows,
    "the item rows and the text-only rows fill the classic page"
);

consteval bool rows_partition_the_classic_page() {
    std::array<bool, kClassicPageRows> drawn{};
    const auto claim = [&drawn](proto::OsdRow row) {
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            if (row != proto::OsdRow::saturating(i))
                continue;
            if (drawn[i])
                return false;
            drawn[i] = true;
            return true;
        }
        return false;
    };
    for (const proto::OsdRow row : kTextOnlyLines)
        if (!claim(row))
            return false;
    for (const SystemRowFact& fact : kSystemRows)
        if (!claim(fact.line))
            return false;
    for (const bool d : drawn)
        if (!d)
            return false;
    return true;
}

static_assert(rows_partition_the_classic_page(), "the item rows and text rows tile the OSD");
}

template <class Visit>
void SystemPage::for_each_row(Visit&& visit) const {
    std::array<char, kClassicRowCap> s{};

    const auto draw_item = [&](SystemItem it, std::string_view text, bool stippled) {
        visit(system_item_row(it), it, text, stippled);
    };
    const auto draw_label = [&](SystemItem it) {
        draw_item(it, system_row_label(it), false);
    };
    draw_label(SystemItem::Core);

    if (!facts_.os_version.empty()) {
        std::snprintf(
            s.data(),
            s.size(),
            " MiSTer v%s,  OS v%s",
            facts_.version.c_str(),
            facts_.os_version.c_str()
        );
    } else {
        std::snprintf(s.data(), s.size(), "       MiSTer v%s", facts_.version.c_str());
    }
    visit(kLineVersion, std::nullopt, std::string_view{s.data()}, false);
    visit(kLineKernel, std::nullopt, std::string_view{kernel_row(facts_.kernel)}, false);

    std::string_view storage;
    std::string_view switch_to;
    bool switch_stippled = false;
    if (facts_.storage_usb) {
        storage = "        Storage: USB";
        switch_to = "      Switch to SD card";
    } else if (facts_.storage_sd_fallback) {
        storage = " No USB found, using SD card";
        switch_to = "      Switch to SD card";
    } else {
        storage = "      Storage: SD card";
        switch_to = "        Switch to USB";
        switch_stippled = !facts_.usb_mounted;
    }
    visit(kLineStorage, std::nullopt, storage, false);

    const std::string space = "   Available space: "
        + format_free_space(facts_.avail, FreeSpaceStyle::Classic);
    visit(kLineSpace, std::nullopt, space, false);
    draw_item(SystemItem::SwitchStorage, switch_to, switch_stippled);

    visit(kLineGapMid, std::nullopt, std::string_view{}, false);
    draw_label(SystemItem::RemapKeyboard);
    draw_label(SystemItem::DefineJoystick);
    draw_label(SystemItem::Scripts);
    draw_label(SystemItem::Help);

    draw_item(
        SystemItem::SaveSettings,
        session_.core_loaded ? system_row_label(SystemItem::SaveSettings) : std::string_view{},
        false
    );

    if (const auto tail = reboot_.tail()) {
        draw_item(SystemItem::Reboot, *tail, false);
    } else {
        draw_label(SystemItem::Reboot);
    }
    const std::string autosave_row = std::string{kRowAutosavePrefix}.append(
        autosave_word(session_.autosave)
    );
    draw_item(SystemItem::Autosave, autosave_row, false);

    if (session_.shows_hd_switch()) {
        draw_label(SystemItem::UseHd);
    } else {
        visit(system_item_row(SystemItem::UseHd), std::nullopt, std::string_view{}, false);
    }
    draw_label(SystemItem::Exit);
}

bool SystemPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(SystemPage);
    (void)out.page.title.assign(kTitle);
    out.title_arrow = OsdRenderer::kArrowLeft;
    out.count = 0;

    RowCount total{};
    RowIndex selected{};
    for_each_row([&](proto::OsdRow, std::optional<SystemItem> item, std::string_view text, bool) {
        if (text.empty())
            return;
        if (item.has_value() && item_selected(*item))
            selected = row_after(total);
        total += kOneRow;
    });

    const app::hd::HdWindow win = out.window(selected, total);

    const RowRange window = rows(win.start, win.len);
    RowIndex at{};
    for_each_row([&](proto::OsdRow row,
                     std::optional<SystemItem> item,
                     std::string_view text,
                     bool stippled) {
        if (text.empty())
            return;
        const RowIndex index = at;
        at += kOneRow;
        if (!window.contains(index))
            return;
        PageRow& r = out.rows[out.count++];
        r.classic = classic_row(
            row,
            text,
            {.inverted = item.has_value() && item_selected(*item), .stippled = stippled}
        );
        r.has_hd = true;

        const SystemRowFact* const fact = item.has_value() ? find_system_row(*item) : nullptr;
        if (fact != nullptr && !fact->hd_label.empty()) {
            (void)r.hd.label.assign(fact->hd_label);
            (void)r.hd.value.assign(fact->hd_value);
        } else {
            (void)r.hd.label.assign(plain_label(text));
        }
        if (item == SystemItem::Autosave) {
            (void)r.hd.label.assign(kAutosaveLabel);
            (void)r.hd.value.assign(autosave_word(session_.autosave));
        }
        r.hd.kind = system_row_kind(item);
        r.hd.enabled = !stippled;
        r.hd.selected = item.has_value() && item_selected(*item);
    });

    at = RowIndex{};
    for_each_row([&](proto::OsdRow row,
                     std::optional<SystemItem> item,
                     std::string_view text,
                     bool stippled) {
        if (!text.empty()) {
            const RowIndex index = at;
            at += kOneRow;
            if (window.contains(index))
                return;
        }
        PageRow& r = out.rows[out.count++];
        r.classic = classic_row(
            row,
            text,
            {.inverted = item.has_value() && item_selected(*item), .stippled = stippled}
        );
        r.has_hd = false;
    });
    return true;
}

Transition SystemPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(SystemPage);

    if (ev.buttons.intersects({UiEvent::Menu})) {
        if (session_.front_end && session_.tree == OsdTree::Classic)
            return open_cores_(false);
        return Transition::close_all();
    }

    if (ev.buttons.intersects({UiEvent::Left})) {
        (void)reboot_release(ev);

        if (session_.front_end)
            return Transition{};
        return Transition::replace(PageId::CoreOptions);
    }

    if (ev.buttons.intersects({UiEvent::Recent}) && item_selected(SystemItem::Core)) {
        return open_cores_(true);
    }
    if (!ev.buttons.intersects({UiEvent::Select}))
        return reboot_release(ev);

    const auto item = system_item_of(sel_);
    if (!item) {
        refusals_.add();
        return Transition{};
    }
    switch (*item) {
        case SystemItem::Core:

            return open_cores_(false);
        case SystemItem::Exit:
            return Transition::leave();
        case SystemItem::Scripts:
            return open_scripts_();
        case SystemItem::SaveSettings: {

            if (!session_.core_loaded) {
                refusals_.add();
                return Transition{};
            }

            if (!push_ask(ui_requests_, app::UiRequest::SaveConfig{})) {
                refusals_.add();
            } else {
                ++published_;
            }
            return Transition::replace(PageId::CoreOptions);
        }
        case SystemItem::Reboot: {

            const RebootHold::Step step = reboot_.hold({ev.repeat_depth});
            if (step.ask_cold && ask_reboot_())
                reboot_.ask_landed();
            return Transition{};
        }
        case SystemItem::Autosave:
            return toggle_autosave_();
        case SystemItem::UseHd:
            return use_hd_();
        case SystemItem::DefineJoystick:

            return Transition::push(PageId::JoyMap);
        case SystemItem::SwitchStorage:
        case SystemItem::RemapKeyboard:
        case SystemItem::Help:

            refusals_.add();
            return Transition{};
        case SystemItem::kCount:

            break;
    }

    refusals_.add();
    return Transition{};
}

RebootHold::Step RebootHold::hold(RepeatDepth depth) noexcept {
    TASTY_SEAT_BODY(RebootHold);
    if (phase_ == Phase::Idle)
        phase_ = Phase::Sliding;
    slide_ = ColdSlide::at(depth);

    return Step{slide_, phase_ != Phase::ColdSent && ColdSlide::past_head(depth)};
}

bool RebootHold::release(RepeatDepth depth) noexcept {
    TASTY_SEAT_BODY(RebootHold);
    if (phase_ == Phase::Idle || !depth.released())
        return false;
    phase_ = Phase::Idle;
    slide_ = ColdSlide{};
    return true;
}

Transition SystemPage::reboot_release(const UiEvent& ev) {
    if (!reboot_.release({ev.repeat_depth}))
        return Transition{};
    const app::CmdVerb::LoadCore menu{app::kFrontEndImage, app::CmdVerb::RbfOrMra::Rbf};
    if (route_ && route_->on(menu)) {
        ++published_;
    } else {
        refusals_.add();
    }
    return Transition{};
}

Transition SystemPage::use_hd_() {
    if (!session_.shows_hd_switch()) {
        refusals_.add();
        return Transition{};
    }
    if (!push_ask(ui_requests_, app::UiRequest::SetOsdStyle{.style = app::OsdStyle::Hd})) {
        refusals_.add();
    } else {
        ++published_;
    }
    return Transition::choose_hd_style();
}

Transition SystemPage::toggle_autosave_() {
    if (!push_ask(ui_requests_, app::UiRequest::SetAutosave{.on = !session_.autosave})) {
        refusals_.add();
    } else {
        ++published_;
    }
    return Transition{};
}

bool SystemPage::ask_reboot_() {
    if (!push_ask(ui_requests_, app::UiRequest::Reboot{})) {
        refusals_.add();
        return false;
    }
    ++published_;
    return true;
}

Transition SystemPage::open_root_child(PageContext& ctx) {
    TASTY_SEAT_BODY(SystemPage);
    return cores_.open(ctx, false);
}

Transition SystemPage::open_scripts_() {
    TASTY_SEAT_BODY(SystemPage);
    if (!ctx_) {
        refusals_.add();
        return Transition{};
    }
    return scripts_.open(*ctx_);
}

Transition SystemPage::open_cores_(bool recents_first) {
    TASTY_SEAT_BODY(SystemPage);
    if (!ctx_) {
        refusals_.add();
        return Transition{};
    }
    return cores_.open(*ctx_, recents_first);
}

std::unique_ptr<SystemPage> make_system_page(
    const SystemFacts& facts,
    const SessionFacts& session
) {
    return std::make_unique<SystemPage>(facts, session);
}

const char* system_row_text(SystemRow r) noexcept {

    const auto text = [](SystemItem it) {
        const std::string_view label = system_row_label(it);
        return label.empty() ? "" : label.data();
    };
    switch (r) {
        case SystemRow::Title:
            return kTitle;
        case SystemRow::Core:
            return text(SystemItem::Core);
        case SystemRow::Remap:
            return text(SystemItem::RemapKeyboard);
        case SystemRow::Joystick:
            return text(SystemItem::DefineJoystick);
        case SystemRow::Scripts:
            return text(SystemItem::Scripts);
        case SystemRow::Help:
            return text(SystemItem::Help);
        case SystemRow::Save:
            return text(SystemItem::SaveSettings);
        case SystemRow::Reboot:
            return text(SystemItem::Reboot);
        case SystemRow::UseHd:
            return text(SystemItem::UseHd);
        case SystemRow::Exit:
            return text(SystemItem::Exit);
    }
    return "";
}

OsdRow system_item_row(SystemItem it) noexcept {
    const SystemRowFact* const row = find_system_row(it);
    return row ? row->line : OsdRow{};
}

}
