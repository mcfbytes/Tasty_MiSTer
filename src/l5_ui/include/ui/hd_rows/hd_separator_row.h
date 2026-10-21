// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "ui/hd_row.h"

namespace mister::ui {

class HdSeparatorRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdSeparatorRow() noexcept = default;

    app::PageDescription::Row describe() const override;
    Transition select() override;
};

}
