// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

struct SpiSample {

    static constexpr std::uint16_t kMenuButton = 1u << 0;
    static constexpr std::uint16_t kUserButton = 1u << 1;

    bool ready = false;
    std::uint16_t buttons = 0;
    bool hdmi = false;
};

}  // namespace mister::hal
