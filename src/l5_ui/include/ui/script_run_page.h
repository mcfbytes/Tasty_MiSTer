// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/seat.h"
#include "ui/page.h"
#include "ui/script_launch.h"
#include "ui/script_runner.h"

namespace mister::ui {

class IScriptPolicy;

class ScriptRunPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit ScriptRunPage(ScriptRequest request) noexcept;

    PageId id() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return PageId::ScriptRun;
    }

    void on_enter(PageContext&) override;

    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;

    Transition on_tick(PageContext&) override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return MenuMask{};
    }

    bool bt_pairing_active() const override;

    bool holds_live_script() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return !runner_.finished();
    }

    bool takes_osd_button_backspace() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return true;
    }

    bool runs_a_process() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return runner_.running();
    }

    const ScriptRunner& runner() const noexcept { return runner_; }

    std::string_view argv0() const noexcept { return request_.argv0(); }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return refusals_;
    }

private:
    enum class Phase : std::uint8_t { Run, RenewPaint, RenewDone };

    Transition exit_transition() const;

    const IScriptPolicy& policy_;
    ScriptRequest request_;
    Phase phase_ = Phase::Run;
    ScriptRunner runner_;
    std::uint32_t refusals_ = 0;
};

}
