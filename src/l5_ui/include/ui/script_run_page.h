// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/seat.h"
#include "ui/page.h"
#include "ui/script_launch.h"
#include "ui/script_runner.h"
#include "svc/types.h"

namespace mister::ui {

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

    svc::Tally refusals() const noexcept override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return refusals_;
    }

private:
    Transition exit_transition() const;

    ScriptRequest request_;
    ScriptRunner runner_;
    svc::Tally refusals_{};
};

}
