// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "infra/seat.h"
#include "ui/page.h"
#include "ui/script_runner.h"

namespace mister::ui {

class IScriptPolicy;

class ScriptRunPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    ScriptRunPage(ScriptKind kind, std::string argv0, bool bt_reset = false) noexcept;

    PageId id() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return PageId::ScriptRun;
    }

    void on_enter(PageContext&) override;
    void render(OsdRenderer& out) const override;
    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;

    Transition on_tick(PageContext&) override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return MenuMask{};
    }

    RowCount osd_height() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return RowCount{16};
    }

    bool bt_pairing_active() const override;

    bool holds_pairing_hotkey() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return !runner_.finished();
    }

    bool runs_a_process() const override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return runner_.running();
    }

    const ScriptRunner& runner() const noexcept { return runner_; }

    std::string_view argv0() const noexcept { return argv0_; }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(ScriptRunPage);
        return refusals_;
    }

private:
    enum class Phase : std::uint8_t { Run, RenewPaint, RenewDone };

    Transition exit_transition() const;

    ScriptKind kind_;
    const IScriptPolicy& policy_;
    std::string argv0_;
    bool bt_reset_;
    Phase phase_ = Phase::Run;
    ScriptRunner runner_;
    std::uint32_t refusals_ = 0;
};

}
