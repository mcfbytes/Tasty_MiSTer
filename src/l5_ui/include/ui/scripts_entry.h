// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/types.h"
#include "ui/browser_session.h"
#include "ui/confirm_host.h"
#include "ui/page.h"
#include "ui/page_launches.h"
#include "ui/script_pick_host.h"
#include "ui/session_facts.h"

namespace mister::app {
class NameConfig;
}

namespace mister::ui {

class IBrowseSource;
struct BrowserLaunch;
struct PageContext;

class ScriptsEntry final : private IConfirmHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit ScriptsEntry(const SessionFacts& session) noexcept : session_(session) {}

    void bind(PageContext& ctx);

    Transition open(PageContext& ctx);

    void on_facts(const SessionFacts& session) noexcept { session_ = session; }

    svc::Tally refusals() const noexcept {
        TASTY_SEAT_BODY(ScriptsEntry);
        return refusals_.sum(script_pick_.refusals());
    }

private:
    infra::OptRef<BrowserLaunch> browser_() const noexcept {
        return launches_ ? launches_->browser : infra::OptRef<BrowserLaunch>{};
    }

    Transition open_scripts_confirm();

    std::optional<BrowserSession::Request> arm_scripts_browser_();

    Transition on_cancel() override {
        TASTY_SEAT_BODY(ScriptsEntry);
        return Transition::pop();
    }

    Transition on_confirm(RowIndex choice) override;

    SessionFacts session_{};
    svc::Tally refusals_{};
    infra::OptRef<PageLaunches> launches_{};
    infra::OptRef<const IBrowseSource> storage_{};
    infra::OptRef<app::NameConfig> names_{};
    ScriptPickHost script_pick_{};
};

}
