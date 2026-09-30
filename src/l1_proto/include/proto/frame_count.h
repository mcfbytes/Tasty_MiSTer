// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct FrameCount {
    bool supported = false;
    std::uint8_t count = 0;
};

}  // namespace mister::proto
