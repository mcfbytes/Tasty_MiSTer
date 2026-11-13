// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/page_description.h"
#include "ui/slot_id.h"
#include "ui/text_entry_result.h"
#include "ui/transition.h"
#include "ui/types.h"

namespace mister::ui {

class IHdSlots {
public:
    virtual ~IHdSlots() = default;

    [[nodiscard]] virtual bool slot_shown(SlotId slot) const = 0;
    [[nodiscard]] virtual app::PageDescription::Row slot_describe(SlotId slot) const = 0;
    virtual Transition slot_select(SlotId slot) = 0;

    virtual Transition slot_confirmed(SlotId slot, RowIndex choice) {
        (void)slot;
        (void)choice;
        return Transition::pop();
    }

    virtual Transition slot_entered(SlotId slot, const TextEntryResult& result) {
        (void)slot;
        (void)result;
        return Transition::pop();
    }

protected:
    IHdSlots() = default;
    IHdSlots(const IHdSlots&) = default;
    IHdSlots& operator=(const IHdSlots&) = default;
};

}
