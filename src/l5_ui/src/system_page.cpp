// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/system_page.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string_view>

#include "app/cmd_verb_sink.h"
#include "app/hd_window.h"
#include "app/page_description.h"
#include "app/pending_load.h"
#include "app/name_config.h"
#include "app/recents_store.h"
#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "ui/draw_classic.h"
#include "ui/free_space.h"
#include "ui/osd_renderer.h"
#include "ui/page_ask.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/browse_source.h"
#include "ui/browser_entry.h"
#include "ui/browser_host.h"
#include "ui/browser_launch.h"
#include "ui/browser_session.h"
#include "ui/core_pick_host.h"
#include "ui/confirm_host.h"
#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/page_context.h"
#include "ui/page_launches.h"
#include "ui/script_kind.h"
#include "ui/script_launch.h"
#include "ui/script_pick_host.h"
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

app::PageDescription::RowKind system_row_kind(std::optional<SystemItem> item) noexcept {
    if (!item.has_value())
        return app::PageDescription::RowKind::Text;
    switch (*item) {
        case SystemItem::Core:
        case SystemItem::DefineJoystick:
        case SystemItem::Scripts:
        case SystemItem::SaveSettings:
            return app::PageDescription::RowKind::Submenu;
        case SystemItem::SwitchStorage:
        case SystemItem::RemapKeyboard:
        case SystemItem::Help:
        case SystemItem::Reboot:
        case SystemItem::UseHd:
        case SystemItem::Exit:
            break;
    }
    return app::PageDescription::RowKind::Action;
}

constexpr std::uint8_t kItemRow[10] = {0, 5, 7, 8, 9, 10, 11, 12, 14, 15};

constexpr std::string_view kScriptsWarningBody[] = {
    "",
    "         Attention:",
    " This is dangerous operation!",
    "",
    " Script has control over the",
    " whole system and may damage",
    " the files or settings, then",
    " MiSTer won't boot, so you",
    " will have to re-format the",
    " SD card and fill with files",
    " in order to use it again.",
    "",
    "  Do you want to continue?",
};
constexpr std::string_view kScriptsWarningChoices[] = {
    "            No",
    "            Yes",
    "  Yes, and don't ask again",
};

}

void SystemPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(SystemPage);
    launches_ = infra::OptRef<PageLaunches>{ctx.launches};
    storage_ = ctx.storage;
    route_ = ctx.route;
    ui_requests_ = ctx.ui_requests;
    recents_ = ctx.recents;
    names_ = ctx.names;
    script_pick_.bind({.launches = launches_, .storage = ctx.storage});
    core_pick_.bind({.route = ctx.route, .recents = ctx.recents});
}

namespace {

enum ClassicLine : std::uint8_t {
    kLineCore,
    kLineVersion,
    kLineGapTop,
    kLineStorage,
    kLineSpace,
    kLineSwitch,
    kLineGapMid,
    kLineRemap,
    kLineJoystick,
    kLineScripts,
    kLineHelp,
    kLineSave,
    kLineReboot,
    kLineKernel,
    kLineUseHd,
    kLineExit,
};

constexpr std::string_view kColdHead = " Cold";
constexpr RowIndex kConfirmCancel{0};
constexpr RowIndex kConfirmRememberScripts{2};
}

template <class Visit>
void SystemPage::for_each_row(Visit&& visit) const {
    char s[40];
    visit(kLineCore, SystemItem::Core, std::string_view{kRowCore}, false);

    if (facts_.os_version[0] != '\0') {
        std::snprintf(s, sizeof s, " MiSTer v%s,  OS v%s", facts_.version, facts_.os_version);
    } else {
        std::snprintf(s, sizeof s, "       MiSTer v%s", facts_.version);
    }
    visit(kLineVersion, std::nullopt, std::string_view{s}, false);
    visit(kLineGapTop, std::nullopt, std::string_view{}, false);

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
    visit(kLineSwitch, SystemItem::SwitchStorage, switch_to, switch_stippled);

    visit(kLineGapMid, std::nullopt, std::string_view{}, false);
    visit(kLineRemap, SystemItem::RemapKeyboard, std::string_view{kRowRemap}, false);
    visit(kLineJoystick, SystemItem::DefineJoystick, std::string_view{kRowJoy}, false);
    visit(kLineScripts, SystemItem::Scripts, std::string_view{kRowScripts}, false);
    visit(kLineHelp, SystemItem::Help, std::string_view{kRowHelp}, false);
    visit(
        kLineSave,
        SystemItem::SaveSettings,
        facts_.core_loaded ? std::string_view{kRowSave} : std::string_view{},
        false
    );

    if (reboot_req_) {
        std::snprintf(s, sizeof s, " Cold Reboot");
        visit(
            kLineReboot,
            SystemItem::Reboot,
            std::string_view{s + kColdHead.size() - reboot_off_},
            false
        );
    } else {
        visit(kLineReboot, SystemItem::Reboot, std::string_view{kRowReboot}, false);
    }
    visit(kLineKernel, std::nullopt, std::string_view{kernel_row(facts_.kernel)}, false);

    if (facts_.hd_switchable) {
        visit(kLineUseHd, SystemItem::UseHd, std::string_view{kRowUseHd}, false);
    } else {
        visit(kLineUseHd, std::nullopt, std::string_view{}, false);
    }
    visit(kLineExit, SystemItem::Exit, std::string_view{kStdExit}, false);
}

bool SystemPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(SystemPage);
    (void)out.page.title.assign(kTitle);
    out.title_arrow = OsdRenderer::kArrowLeft;
    out.count = 0;

    std::uint16_t total = 0;
    std::uint16_t selected = 0;
    for_each_row([&](std::uint8_t, std::optional<SystemItem> item, std::string_view text, bool) {
        if (text.empty())
            return;
        if (item.has_value() && item_selected(*item))
            selected = total;
        ++total;
    });

    const app::HdWindow win = app::hd_window(selected, total, out.page.first);
    out.page.total = total;
    out.page.first = win.first;

    std::uint16_t at = 0;
    for_each_row([&](std::uint8_t row,
                     std::optional<SystemItem> item,
                     std::string_view text,
                     bool stippled) {
        if (text.empty())
            return;
        const std::uint16_t index = at++;
        if (index < win.first || index >= win.first + win.count)
            return;
        PageRow& r = out.rows[out.count++];
        r.classic = classic_row(
            OsdRow{row},
            text,
            item.has_value() && item_selected(*item),
            stippled
        );
        r.has_hd = true;
        if (item == SystemItem::Reboot) {
            (void)r.hd.label.assign(kHdRebootLabel);
            (void)r.hd.value.assign(kHdRebootValue);
        } else {
            (void)r.hd.label.assign(plain_label(text));
        }
        r.hd.kind = system_row_kind(item);
        r.hd.enabled = !stippled;
        r.hd.selected = item.has_value() && item_selected(*item);
    });

    at = 0;
    for_each_row([&](std::uint8_t row,
                     std::optional<SystemItem> item,
                     std::string_view text,
                     bool stippled) {
        if (!text.empty()) {
            const std::uint16_t index = at++;
            if (index >= win.first && index < win.first + win.count)
                return;
        }
        PageRow& r = out.rows[out.count++];
        r.classic = classic_row(
            OsdRow{row},
            text,
            item.has_value() && item_selected(*item),
            stippled
        );
        r.has_hd = false;
    });
    return true;
}

Transition SystemPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(SystemPage);

    if (ev.buttons.has_any({UiEvent::Menu})) {
        if (facts_.front_end && !facts_.in_hd_tree)
            return open_core_browser(false);
        return Transition{Transition::Kind::CloseAll};
    }

    if (ev.buttons.has_any({UiEvent::Left})) {
        (void)reboot_release(ev);

        if (facts_.front_end)
            return Transition{Transition::Kind::Stay};
        return Transition{Transition::Kind::Replace, PageId::CoreOptions};
    }

    if (ev.buttons.has_any({UiEvent::Recent}) && item_selected(SystemItem::Core)) {
        return open_core_browser(true);
    }
    if (!ev.buttons.has_any({UiEvent::Select}))
        return reboot_release(ev);

    const auto item = system_item_from_raw(sel_.v);
    if (!item) {
        ++refusals_;
        return Transition{};
    }
    switch (*item) {
        case SystemItem::Core:

            return open_core_browser(false);
        case SystemItem::Exit:
            return Transition{Transition::Kind::Leave};
        case SystemItem::Scripts:
            return open_scripts_confirm();
        case SystemItem::SaveSettings: {

            if (!facts_.core_loaded) {
                ++refusals_;
                return Transition{};
            }

            if (!push_ask(ui_requests_, app::UiRequest::SaveConfig{})) {
                ++refusals_;
            } else {
                ++published_;
            }
            return Transition{Transition::Kind::Replace, PageId::CoreOptions};
        }
        case SystemItem::Reboot: {

            reboot_req_ = true;
            constexpr unsigned kRepeatsPerSlide = 3;
            const unsigned off = ev.repeat_depth / kRepeatsPerSlide;
            if (off > kColdHead.size()) {

                if (!cold_sent_ && ask_reboot_())
                    cold_sent_ = true;
            } else {
                reboot_off_ = static_cast<std::uint8_t>(off);
            }
            return Transition{};
        }
        case SystemItem::UseHd:
            return use_hd_();
        case SystemItem::DefineJoystick:

            return Transition{Transition::Kind::Push, PageId::JoyMap};
        case SystemItem::SwitchStorage:
        case SystemItem::RemapKeyboard:
        case SystemItem::Help:

            ++refusals_;
            return Transition{};
    }

    ++refusals_;
    return Transition{};
}

Transition SystemPage::reboot_release(const UiEvent& ev) {
    if (reboot_req_ && ev.repeat_depth == 0) {
        reboot_req_ = false;
        cold_sent_ = false;
        reboot_off_ = 0;
        const app::CmdVerb::LoadCore menu{app::kFrontEndImage, app::CmdVerb::RbfOrMra::Rbf};
        if (route_ && route_->on(menu)) {
            ++published_;
        } else {
            ++refusals_;
        }
    }
    return Transition{};
}

Transition SystemPage::use_hd_() {
    if (!facts_.hd_switchable) {
        ++refusals_;
        return Transition{};
    }
    if (!push_ask(ui_requests_, app::UiRequest::SetOsdStyle{.style = 1})) {
        ++refusals_;
    } else {
        ++published_;
    }
    return Transition{Transition::Kind::Replace, PageId::HdMenu};
}

bool SystemPage::ask_reboot_() {
    if (!push_ask(ui_requests_, app::UiRequest::Reboot{})) {
        ++refusals_;
        return false;
    }
    ++published_;
    return true;
}

Transition SystemPage::open_scripts_confirm() {
    if (names_ && names_->scripts_confirm_skippable()) {
        return open_scripts_browser(Transition::Kind::Push);
    }
    if (!launches_ || !launches_->confirm) {
        ++refusals_;
        return Transition{};
    }
    ConfirmPage::Spec spec;
    spec.title = "Warning!!!";
    spec.body = kScriptsWarningBody;
    spec.choices = kScriptsWarningChoices;
    spec.host = infra::opt_ref_of(static_cast<IConfirmHost*>(this));
    launches_->confirm->pending = spec;
    return Transition{Transition::Kind::Push, PageId::Confirm};
}

Transition SystemPage::open_scripts_browser(Transition::Kind kind) {
    const infra::OptRef<BrowserLaunch> browser = browser_();
    if (!browser || !storage_) {
        ++refusals_;
        return kind == Transition::Kind::Replace ? Transition{Transition::Kind::Pop} : Transition{};
    }
    BrowserSession::Request r;
    r.extensions = "SH";
    r.scan.include_dirs = true;

    r.home_path = "Scripts";
    r.games_dir = facts_.core_dir;
    r.host = infra::opt_ref_of(static_cast<IBrowserHost*>(&script_pick_));
    browser->pending = static_cast<BrowserSession::Request&&>(r);

    return Transition{kind, PageId::Browser};
}

Transition SystemPage::open_root_child(PageContext&) {
    TASTY_SEAT_BODY(SystemPage);
    return open_core_browser(false);
}

Transition SystemPage::open_scripts(PageContext&) {
    TASTY_SEAT_BODY(SystemPage);
    return open_scripts_confirm();
}

Transition SystemPage::open_core_browser(bool recents_first) {
    const infra::OptRef<BrowserLaunch> browser = browser_();
    if (!browser || !storage_) {
        ++refusals_;
        return Transition{};
    }
    BrowserSession::Request r;
    r.extensions = "RBFMRAMGL";
    r.scan.cores_only = true;
    r.recents = app::RecentsStore::ListId{app::RecentsStore::ListId::Kind::Cores, 0};
    r.recents_first = recents_first;
    r.host = infra::opt_ref_of(static_cast<IBrowserHost*>(&core_pick_));
    if (recents_first) {

        if (!recents_
            || recents_
                   ->load("", app::RecentsStore::ListId{app::RecentsStore::ListId::Kind::Cores, 0})
                   .empty()) {
            ++refusals_;
            return Transition{};
        }
    }
    browser->pending = static_cast<BrowserSession::Request&&>(r);
    return Transition{Transition::Kind::Push, PageId::Browser};
}

Transition SystemPage::on_confirm(RowIndex choice) {
    TASTY_SEAT_BODY(SystemPage);
    if (choice == kConfirmCancel)
        return Transition{Transition::Kind::Pop};
    if (choice == kConfirmRememberScripts) {

        if (!names_ || !names_->remember_scripts_confirm())
            ++refusals_;
    }
    return open_scripts_browser(Transition::Kind::Replace);
}

std::unique_ptr<Page> make_system_page(const SystemFacts& facts) {
    return std::unique_ptr<Page>(new SystemPage(facts));
}

const char* system_row_text(SystemRow r) noexcept {
    switch (r) {
        case SystemRow::Title:
            return kTitle;
        case SystemRow::Core:
            return kRowCore;
        case SystemRow::Remap:
            return kRowRemap;
        case SystemRow::Joystick:
            return kRowJoy;
        case SystemRow::Scripts:
            return kRowScripts;
        case SystemRow::Help:
            return kRowHelp;
        case SystemRow::Save:
            return kRowSave;
        case SystemRow::Reboot:
            return kRowReboot;
        case SystemRow::UseHd:
            return kRowUseHd;
        case SystemRow::Exit:
            return kStdExit;
    }
    return "";
}

OsdRow system_item_row(SystemItem it) noexcept {
    const auto i = static_cast<unsigned>(it);
    return OsdRow{(i < std::size(kItemRow)) ? kItemRow[i] : std::uint8_t{0}};
}

}
