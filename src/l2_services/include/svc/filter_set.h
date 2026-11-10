// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/filter_bank.h"
#include "svc/filter_store.h"
#include "svc/video_timing.h"

namespace mister::svc {

struct FilterSet {

    struct Slot {
        FilterBank bank;
        ScalerSlotMode mode{};
    };

    ScalerSlots<Slot> slots{};

    [[nodiscard]] const FilterBank& bank(VFilter f) const noexcept {
        return slots[ScalerSlotIndex::of(f)].bank;
    }

    [[nodiscard]] ScalerSlotMode filter_mode() const noexcept {
        return slots[ScalerSlotIndex::of(VFilter::Horz)].mode;
    }

    [[nodiscard]] ScalerSlots<bool> enabled() const noexcept {
        ScalerSlots<bool> out{};
        for (const ScalerSlotIndex slot : kScalerSlotOrder)
            out[slot] = slots[slot].mode.enabled();
        return out;
    }
};

}
