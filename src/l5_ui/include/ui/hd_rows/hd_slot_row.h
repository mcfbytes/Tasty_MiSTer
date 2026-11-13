// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/page_description.h"
#include "infra/seat.h"
#include "ui/hd_row.h"
#include "ui/hd_slots.h"
#include "ui/slot_id.h"

namespace mister::ui {

class HdSlotRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdSlotRow(IHdSlots& owner, SlotId slot) noexcept : owner_(&owner), slot_(slot) {}

    bool shown() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;

private:
    IHdSlots* owner_;
    SlotId slot_;
};

}
