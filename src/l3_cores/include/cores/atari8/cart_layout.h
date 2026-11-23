// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::atari8 {

enum class CartLayout : std::uint8_t {
    Plain,
    Oss034M,
    Oss043M,
    Corina,
    TwoChip,
    OneChip,
    Mirrored,
    SuperCart,
    BountyBob,
};

}
