// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/atari8/atari8_machine.h"
#include "proto/types.h"

namespace mister::cores::atari8 {

enum class CartSlot : std::uint8_t { First, Second };

inline constexpr proto::IoIndex kComputerCart{8};
inline constexpr proto::IoIndex kComputerSecondCart{9};
inline constexpr proto::IoIndex kConsoleCart{1};

[[nodiscard]] constexpr std::optional<CartSlot> cart_slot_of(
    Atari8Machine machine,
    proto::IoIndex io
) noexcept {
    switch (machine) {
        case Atari8Machine::Computer:
            if (io == kComputerCart)
                return CartSlot::First;
            if (io == kComputerSecondCart)
                return CartSlot::Second;
            return std::nullopt;
        case Atari8Machine::Console:
            break;
    }
    if (io == kConsoleCart)
        return CartSlot::First;
    return std::nullopt;
}

}
