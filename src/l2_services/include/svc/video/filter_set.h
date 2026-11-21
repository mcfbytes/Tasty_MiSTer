// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/video/filter_bank.h"
#include "svc/video/filter_store.h"
#include "svc/video/video_timing.h"

namespace mister::svc::video {

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
