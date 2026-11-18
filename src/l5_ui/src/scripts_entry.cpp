// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/scripts_entry.h"

#include <string_view>
#include <utility>

#include "app/name_config.h"
#include "infra/opt_ref.h"
#include "ui/browse_source.h"
#include "ui/browser_launch.h"
#include "ui/browser_session.h"
#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/page_context.h"
#include "ui/page_launches.h"

namespace mister::ui {

namespace {

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
constexpr RowIndex kConfirmCancel{0};
constexpr RowIndex kConfirmRememberScripts{2};

}

void ScriptsEntry::bind(PageContext& ctx) {
    TASTY_SEAT_BODY(ScriptsEntry);
    launches_ = infra::OptRef<PageLaunches>{ctx.launches};
    storage_ = ctx.storage;
    names_ = ctx.names;
    script_pick_.bind({.launches = launches_, .storage = ctx.storage});
}

Transition ScriptsEntry::open(PageContext& ctx) {
    TASTY_SEAT_BODY(ScriptsEntry);
    bind(ctx);
    return open_scripts_confirm();
}

Transition ScriptsEntry::open_scripts_confirm() {
    TASTY_SEAT_BODY(ScriptsEntry);
    if (names_ && names_->scripts_confirm_skippable()) {
        std::optional<BrowserSession::Request> spec = arm_scripts_browser_();
        if (!spec)
            return Transition{};
        return browser_()->open(PageId::Browser, std::move(*spec));
    }
    if (!launches_ || !launches_->confirm) {
        refusals_.add();
        return Transition{};
    }
    ConfirmPage::Spec spec;
    spec.title = "Warning!!!";
    spec.body = kScriptsWarningBody;
    spec.choices = kScriptsWarningChoices;
    spec.host = infra::opt_ref_of(static_cast<IConfirmHost*>(this));
    return launches_->confirm->open(PageId::Confirm, spec);
}

std::optional<BrowserSession::Request> ScriptsEntry::arm_scripts_browser_() {
    TASTY_SEAT_BODY(ScriptsEntry);
    const infra::OptRef<BrowserLaunch> browser = browser_();
    if (!browser || !storage_) {
        refusals_.add();
        return std::nullopt;
    }
    BrowserSession::Request r;
    r.extensions = "SH";
    r.scan.include_dirs = true;

    r.home_path = "Scripts";
    r.games_dir.assign(session_.core_dir.view());
    r.host = infra::opt_ref_of(static_cast<IBrowserHost*>(&script_pick_));

    return r;
}

Transition ScriptsEntry::on_confirm(RowIndex choice) {
    TASTY_SEAT_BODY(ScriptsEntry);
    if (choice == kConfirmCancel)
        return Transition::pop();
    if (choice == kConfirmRememberScripts) {

        if (!names_ || !names_->remember_scripts_confirm())
            refusals_.add();
    }
    std::optional<BrowserSession::Request> spec = arm_scripts_browser_();
    if (!spec)
        return Transition::pop();
    return browser_()->open(PageId::Browser, std::move(*spec), true);
}

}
