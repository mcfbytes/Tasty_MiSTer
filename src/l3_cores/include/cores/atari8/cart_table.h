// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "cores/atari8/cart_row.h"
#include "cores/atari8/cart_size_test.h"

namespace mister::cores::atari8 {

struct CartTable {
    std::span<const CartRow> rows{};
    CartSizeTest size_test = CartSizeTest::WholeKib;
    bool sdx_signature = false;
};

}
