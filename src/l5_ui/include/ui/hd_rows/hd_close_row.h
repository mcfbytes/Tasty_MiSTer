// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "app/hd/page_description.h"
#include "app/hd/tier_set.h"
#include "infra/seat.h"
#include "ui/row.h"

namespace mister::ui {

class HdCloseRow final : public IRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::string_view label;
        app::hd::RowTier tier = app::hd::RowTier::Place;
    };

    explicit HdCloseRow(const Spec& spec) noexcept : spec_(spec) {}

    app::hd::PageDescription::Row describe(const RowCx&) const override;
    Transition select(const RowCx&) override;

private:
    Spec spec_;
};

}
