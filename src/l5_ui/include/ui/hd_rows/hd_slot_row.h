// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/hd/page_description.h"
#include "infra/seat.h"
#include "ui/row.h"
#include "ui/hd_slots.h"
#include "ui/slot_id.h"

namespace mister::ui {

class HdSlotRow final : public IRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdSlotRow(IHdSlots& owner, SlotId slot) noexcept : owner_(&owner), slot_(slot) {}

    bool shown() const override;
    app::hd::PageDescription::Row describe(const RowCx&) const override;
    Transition select(const RowCx&) override;

private:
    IHdSlots* owner_;
    SlotId slot_;
};

}
