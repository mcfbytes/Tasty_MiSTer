// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "app/ini_cell.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/page.h"

namespace mister::app {
class UiRequestRing;
}

namespace mister::ui {

class MiscPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit MiscPage(const app::IniCell& ini) noexcept : ini_(ini) {}

    PageId id() const override {
        TASTY_SEAT_BODY(MiscPage);
        return PageId::Misc;
    }

    void on_enter(PageContext& ctx) override;
    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;
    PageFacts facts() const override;
    MenuMask mask() const override;

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(MiscPage);
        sel_ = i;
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(MiscPage);
        return sel_;
    }

    [[nodiscard]] std::string row_text(proto::OsdRow row) const;
    [[nodiscard]] bool present(svc::IniChoice c) const noexcept;
    void cycle(bool forward);

    [[nodiscard]] svc::IniChoice choice() const noexcept {
        TASTY_SEAT_BODY(MiscPage);
        return choice_;
    }

private:
    void switch_to_();

    const app::IniCell& ini_;
    infra::OptRef<app::UiRequestRing> asks_{};
    svc::IniChoice choice_ = svc::IniChoice::Main;
    RowIndex sel_{};
};

}
