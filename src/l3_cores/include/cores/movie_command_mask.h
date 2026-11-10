// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct MovieCommandMask {
    static constexpr std::uint8_t kReset = 0x1;
    static constexpr std::uint8_t kPower = 0x2;
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr MovieCommandMask of_bits(std::uint8_t bits) noexcept {
        return MovieCommandMask{bits};
    }

    [[nodiscard]] constexpr MovieCommandMask with_reset() const noexcept {
        return MovieCommandMask{static_cast<std::uint8_t>(v | kReset)};
    }

    [[nodiscard]] constexpr MovieCommandMask with_power() const noexcept {
        return MovieCommandMask{static_cast<std::uint8_t>(v | kPower)};
    }

    [[nodiscard]] constexpr bool has_any() const noexcept { return v != 0; }

    friend constexpr bool operator==(MovieCommandMask, MovieCommandMask) = default;
};

}
