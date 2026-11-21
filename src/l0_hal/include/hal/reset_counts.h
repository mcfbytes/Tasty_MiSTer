// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_field.h"
#include "hal/reg_word.h"
#include "hal/types.h"

namespace mister::hal {

struct ResetCounts {
    static constexpr RegField<std::uint32_t> kHrstField{0, 0xFF};
    static constexpr RegField<std::uint32_t> kFreedField{8, 0xFF};

    EventCount8 hrst{};
    EventCount8 freed{};

    [[nodiscard]] static constexpr ResetCounts of(RegWord w) noexcept {
        return ResetCounts{
            .hrst = EventCount8::of_lane(kHrstField.get(w)),
            .freed = EventCount8::of_lane(kFreedField.get(w))
        };
    }

    friend constexpr bool operator==(ResetCounts, ResetCounts) = default;
};

}
