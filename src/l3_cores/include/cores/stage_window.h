// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct StageWindow {
    std::uint8_t index = 0;
    std::uint32_t address = 0;
    std::uint32_t bytes = 0;
};

}
