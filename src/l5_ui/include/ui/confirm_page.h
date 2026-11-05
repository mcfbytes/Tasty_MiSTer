// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/page.h"

namespace mister::ui {

class IConfirmHost;

class ConfirmPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::string_view title;
        std::span<const std::string_view> body;
        std::span<const std::string_view> choices;
        infra::OptRef<IConfirmHost> host{};

        RowIndex initial{};
    };

    explicit ConfirmPage(Spec spec) noexcept : spec_(spec), sel_{spec_.initial} {}

    PageId id() const override {
        TASTY_SEAT_BODY(ConfirmPage);
        return PageId::Confirm;
    }

    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(ConfirmPage);
        const RowCount n = RowCount::of(spec_.choices.size());
        if (n >= menu_mask_rows())
            return MenuMask{};
        return dense_mask(n);
    }

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(ConfirmPage);
        sel_ = i;
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(ConfirmPage);
        return sel_;
    }

private:
    Spec spec_;
    RowIndex sel_{};
};

}
