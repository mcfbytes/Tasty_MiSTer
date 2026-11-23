// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "ui/fb_terminal_host.h"
#include "ui/page.h"
#include "ui/script_request.h"
#include "ui/terminal_script.h"

namespace mister::ui {

class FbTerminalPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    FbTerminalPage(ScriptRequest request, infra::OptRef<FbTerminalHost> host) noexcept;

    ~FbTerminalPage() override;

    PageId id() const override;
    void on_enter(PageContext& ctx) override;
    bool walk(PageWalk& out) const override;
    bool describe(app::hd::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;
    Transition on_tick(PageContext& ctx) override;
    MenuMask mask() const override;
    PageFacts facts() const override;
    bool holds_live_script() const override;
    bool runs_a_process() const override;
    infra::Tally refusals() const noexcept override;

private:
    Transition fall_back_(PageContext& ctx);

    TerminalPhase phase_ = TerminalPhase::Idle;
    ScriptRequest request_;
    TerminalScript script_;
    infra::OptRef<FbTerminalHost> host_{};
    infra::Tally refusals_{};
};

}
