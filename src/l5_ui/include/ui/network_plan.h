// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/seat.h"
#include "ui/connect_slot_plan.h"

namespace mister::ui {

class NetworkPlan final : public ConnectSlotPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit NetworkPlan(const HdPlanInputs& in);

    PageId id() const override;
    std::string_view title() const override;
    RowCount pinned() const noexcept override;
    ConnectDemand connect_demand() const override;

    bool slot_shown(SlotId slot) const override;
    app::PageDescription::Row slot_describe(SlotId slot) const override;
    Transition slot_select(SlotId slot) override;
};

}
