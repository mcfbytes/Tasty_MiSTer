// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/system_page.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

#include "app/cmd_verb_sink.h"
#include "app/hd_window.h"
#include "app/page_description.h"
#include "app/pending_load.h"
#include "app/name_config.h"
#include "app/recents_store.h"
#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "ui/free_space.h"
#include "ui/osd_renderer.h"
#include "ui/page_ask.h"
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

template <class Visit>
void SystemPage::for_each_row(Visit&& visit) const {
    char s[40];
    visit(std::uint8_t{0}, SystemItem::Core, std::string_view{kRowCore}, false);

    if (facts_.os_version[0] != '\0') {
        std::snprintf(s, sizeof s, " MiSTer v%s,  OS v%s", facts_.version, facts_.os_version);
    } else {
        std::snprintf(s, sizeof s, "       MiSTer v%s", facts_.version);
    }
    visit(std::uint8_t{1}, std::nullopt, std::string_view{s}, false);
    visit(std::uint8_t{2}, std::nullopt, std::string_view{}, false);

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
    visit(std::uint8_t{3}, std::nullopt, storage, false);

    const std::string space = "   Available space: "
        + format_free_space(facts_.avail_bytes, FreeSpaceStyle::Classic);
    visit(std::uint8_t{4}, std::nullopt, space, false);
    visit(std::uint8_t{5}, SystemItem::SwitchStorage, switch_to, switch_stippled);

    visit(std::uint8_t{6}, std::nullopt, std::string_view{}, false);
    visit(std::uint8_t{7}, SystemItem::RemapKeyboard, std::string_view{kRowRemap}, false);
    visit(std::uint8_t{8}, SystemItem::DefineJoystick, std::string_view{kRowJoy}, false);
    visit(std::uint8_t{9}, SystemItem::Scripts, std::string_view{kRowScripts}, false);
    visit(std::uint8_t{10}, SystemItem::Help, std::string_view{kRowHelp}, false);
    visit(
        std::uint8_t{11},
        SystemItem::SaveSettings,
        facts_.core_loaded ? std::string_view{kRowSave} : std::string_view{},
        false
    );

    if (reboot_req_) {
        std::snprintf(s, sizeof s, " Cold Reboot");
        visit(std::uint8_t{12}, SystemItem::Reboot, std::string_view{s + 5 - reboot_off_}, false);
    } else {
        visit(std::uint8_t{12}, SystemItem::Reboot, std::string_view{kRowReboot}, false);
    }
    visit(std::uint8_t{13}, std::nullopt, std::string_view{kernel_row(facts_.kernel)}, false);

    if (facts_.hd_switchable) {
        visit(std::uint8_t{14}, SystemItem::UseHd, std::string_view{kRowUseHd}, false);
    } else {
        visit(std::uint8_t{14}, std::nullopt, std::string_view{}, false);
    }
    visit(std::uint8_t{15}, SystemItem::Exit, std::string_view{kStdExit}, false);
}

void SystemPage::render_row(
    OsdRenderer& out,
    std::uint8_t row,
    std::optional<SystemItem> item,
    std::string_view text,
    bool stippled
) const {
    out.text(OsdRow{row}, text, item.has_value() && item_selected(*item), stippled);
}

void SystemPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(SystemPage);
    out.set_title(kTitle, OsdRenderer::kArrowLeft);
    for_each_row([&](std::uint8_t row,
                     std::optional<SystemItem> item,
                     std::string_view text,
                     bool stippled) { render_row(out, row, item, text, stippled); });
}

bool SystemPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(SystemPage);
    const std::uint16_t hint = out.first;
    out = {};
    (void)out.title.assign(kTitle);

    std::vector<app::PageDescription::Row> all;
    std::uint16_t selected = 0;
    for_each_row(
        [&](std::uint8_t, std::optional<SystemItem> item, std::string_view text, bool stippled) {
            if (text.empty())
                return;
            app::PageDescription::Row row;
            if (item == SystemItem::Reboot) {
                (void)row.label.assign(kHdRebootLabel);
                (void)row.value.assign(kHdRebootValue);
            } else {
                (void)row.label.assign(plain_label(text));
            }
            row.kind = system_row_kind(item);
            row.enabled = !stippled;
            if (item.has_value() && item_selected(*item)) {
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

Transition SystemPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(SystemPage);

    if ((ev.buttons & UiEvent::Menu) != 0) {
        if (facts_.front_end && !facts_.in_hd_tree)
            return open_core_browser(false);
        return Transition{Transition::Kind::CloseAll};
    }

    if ((ev.buttons & UiEvent::Left) != 0) {
        (void)reboot_release(ev);

        if (facts_.front_end)
            return Transition{Transition::Kind::Stay};
        return Transition{Transition::Kind::Replace, PageId::CoreOptions};
    }

    if ((ev.buttons & UiEvent::Recent) != 0 && item_selected(SystemItem::Core)) {
        return open_core_browser(true);
    }
    if ((ev.buttons & UiEvent::Select) == 0)
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
            const unsigned off = ev.repeat_depth / 3;
            if (off > 5) {

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

Transition SystemPage::on_confirm(std::uint8_t choice) {
    TASTY_SEAT_BODY(SystemPage);
    if (choice == 0)
        return Transition{Transition::Kind::Pop};
    if (choice == 2) {

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

std::uint8_t system_item_row(SystemItem it) noexcept {
    const auto i = static_cast<unsigned>(it);
    return (i < std::size(kItemRow)) ? kItemRow[i] : std::uint8_t{0};
}

}
