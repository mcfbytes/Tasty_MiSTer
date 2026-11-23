// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/atari8/cart_kib.h"
#include "cores/atari8/cart_layout.h"
#include "cores/atari8/cart_mode.h"
#include "cores/atari8/cart_type.h"

namespace mister::cores::atari8 {

struct CartRow {
    CartType type{};
    std::string_view label{};
    CartMode mode{};
    CartKib size{};
    CartLayout layout = CartLayout::Plain;
};

}
