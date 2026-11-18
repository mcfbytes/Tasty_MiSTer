// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::proto {

struct PadMasks {
    JoyMask buttons;
    JoyMask autofire;

    [[nodiscard]] constexpr JoyMask composed() const noexcept { return buttons.with(autofire); }

    friend constexpr bool operator==(PadMasks, PadMasks) = default;
};

}
