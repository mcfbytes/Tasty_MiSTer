// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct StickPos {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    friend constexpr bool operator==(StickPos, StickPos) = default;
};

}
