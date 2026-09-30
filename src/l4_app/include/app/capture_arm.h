// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct CaptureArm {
    std::uint32_t token = 0;
    std::uint8_t armed = 0;
};

}  // namespace mister::app
