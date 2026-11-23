// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/atari8/cart_layout.h"
#include "cores/atari8/cart_mode.h"
#include "cores/atari8/cart_row.h"
#include "cores/atari8/cart_type.h"

namespace mister::cores::atari8 {

struct CartMatch {
    CartMode mode{};
    CartType type{};
    CartLayout layout = CartLayout::Plain;
    std::string_view label{};

    [[nodiscard]] static constexpr CartMatch of(const CartRow& row) noexcept {
        return CartMatch{
            .mode = row.mode,
            .type = row.type,
            .layout = row.layout,
            .label = row.label
        };
    }

    friend constexpr bool operator==(const CartMatch&, const CartMatch&) = default;
};

}
