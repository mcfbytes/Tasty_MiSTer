// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/fact.h"

namespace mister::hal {

struct I2cBusRange {
    std::uint8_t first = 0;
    std::uint8_t last = 0;
};

struct VideoOutDecl {
    Fact<I2cBusRange> i2c_buses;
    const char* vsync_device;
};

}  // namespace mister::hal
