// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct RawKeyEdge {
    std::uint16_t code = 0;
    std::uint8_t pressed = 0;
    std::uint8_t reserved = 0;
};
static_assert(sizeof(RawKeyEdge) == 4, "the ring's slot size is load-bearing");

}  // namespace mister::app
