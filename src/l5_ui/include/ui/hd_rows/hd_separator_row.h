// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "ui/row.h"

namespace mister::ui {

class HdSeparatorRow final : public IRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdSeparatorRow() noexcept = default;

    app::hd::PageDescription::Row describe(const RowCx&) const override;
    Transition select(const RowCx&) override;
};

}
