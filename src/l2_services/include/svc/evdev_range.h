// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/types.h"

namespace mister::svc {

inline void evdev_range_reversed() noexcept {}

class EvdevRange {
public:
    consteval EvdevRange(EvdevCode first, EvdevCode last) : first_(first), last_(last) {
        if (last < first)
            evdev_range_reversed();
    }

    [[nodiscard]] constexpr bool contains(EvdevCode code) const noexcept {
        return first_ <= code && code <= last_;
    }

private:
    EvdevCode first_, last_;
};

}
