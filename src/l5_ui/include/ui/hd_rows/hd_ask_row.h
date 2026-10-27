// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "app/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/confirm_host.h"
#include "ui/hd_row.h"

namespace mister::ui {

class IHdAction;
struct ConfirmLaunch;

class HdAskRow final : public IHdRow, public IConfirmHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::string_view label;
        std::span<const std::string_view> body;
        IHdAction& action;

        infra::OptRef<ConfirmLaunch> confirm{};

        std::uint8_t initial = 1;

        decltype(app::PageDescription::Row::value) value{};
        app::RowTier tier = app::RowTier::Place;
    };

    explicit HdAskRow(const Spec& spec) noexcept : spec_(spec) {}

    bool shown() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;
    Transition on_confirm(std::uint8_t choice) override;

private:
    Spec spec_;
};

}
