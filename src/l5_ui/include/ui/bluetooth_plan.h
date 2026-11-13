// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/seat.h"
#include "ui/connect_slot_plan.h"

namespace mister::ui {

class BluetoothPlan final : public ConnectSlotPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit BluetoothPlan(const HdPlanInputs& in);

    PageId id() const override;
    std::string_view title() const override;
    RowCount pinned() const noexcept override;
    ConnectDemand connect_demand() const override;

    bool slot_shown(SlotId slot) const override;
    app::PageDescription::Row slot_describe(SlotId slot) const override;
    Transition slot_select(SlotId slot) override;
    Transition slot_confirmed(SlotId slot, RowIndex choice) override;
};

}
