// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "app/ini_cell.h"
#include "infra/seat.h"
#include "ui/page.h"

namespace mister::app {
class UiRequestRing;
}

namespace mister::ui {

class SelectIniPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit SelectIniPage(const app::IniCell& ini) noexcept : ini_(ini) {}

    PageId id() const override {
        TASTY_SEAT_BODY(SelectIniPage);
        return PageId::SelectIni;
    }

    void on_enter(PageContext& ctx) override;
    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;

    Transition on_tick(PageContext& ctx) override;
    PageFacts facts() const override;

    [[nodiscard]] std::string row_text(proto::OsdRow row) const;

    [[nodiscard]] bool offers(svc::IniChoice c) const noexcept;

private:
    Transition leave_();
    void switch_to_(svc::IniChoice c);

    const app::IniCell& ini_;
    infra::OptRef<app::UiRequestRing> asks_{};
};

}
