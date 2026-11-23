// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::cores::atari8 {

struct CartMode {
    std::uint8_t v = 0;

    static const CartMode kOff;

    [[nodiscard]] constexpr bool off() const noexcept { return v == 0; }

    [[nodiscard]] constexpr hal::SpiWord value_word() const noexcept { return hal::SpiWord{v}; }

    friend constexpr bool operator==(CartMode, CartMode) = default;
};

inline constexpr CartMode CartMode::kOff{};

}
