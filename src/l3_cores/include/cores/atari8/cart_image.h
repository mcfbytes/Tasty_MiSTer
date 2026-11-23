// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cores/atari8/cart_form.h"
#include "cores/atari8/cart_match.h"

namespace mister::cores::atari8 {

[[nodiscard]] std::vector<std::uint8_t> lay_out_cart(
    const CartMatch& match,
    CartForm form,
    std::span<const std::uint8_t> file
);

}
