// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/atari8/cart_mode.h"

namespace mister::cores::atari8 {

struct CartSelects {
    CartMode first{};
    CartMode second{};
    friend constexpr bool operator==(const CartSelects&, const CartSelects&) = default;
};

}
