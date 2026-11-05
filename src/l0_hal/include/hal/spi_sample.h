// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::hal {

struct SpiSample {

    static constexpr ButtonMask kMenuButton{1u << 0};
    static constexpr ButtonMask kUserButton{1u << 1};

    bool ready = false;
    ButtonMask buttons{};
    bool hdmi = false;
};

}
