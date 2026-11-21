// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "app/hd/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/confirm_host.h"
#include "ui/row.h"

namespace mister::ui {

class IHdAction;
struct ConfirmLaunch;

class HdAskRow final : public IRow, public IConfirmHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::string_view label;
        std::span<const std::string_view> body;
        IHdAction& action;

        infra::OptRef<ConfirmLaunch> confirm{};

        RowIndex initial{1};

        decltype(app::hd::PageDescription::Row::value) value{};
        app::hd::RowTier tier = app::hd::RowTier::Place;
    };

    explicit HdAskRow(const Spec& spec) noexcept : spec_(spec) {}

    bool shown() const override;
    app::hd::PageDescription::Row describe(const RowCx&) const override;
    Transition select(const RowCx&) override;
    Transition on_confirm(RowIndex choice) override;

private:
    Spec spec_;
};

}
