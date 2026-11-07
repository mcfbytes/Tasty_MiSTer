// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct ResWord {
    std::uint16_t v = 0;
    static constexpr std::uint16_t kInterlacedBit = 0x100;
    static constexpr std::uint16_t kRotatedBit = 0x200;

    [[nodiscard]] constexpr bool interlaced() const noexcept { return (v & kInterlacedBit) != 0; }

    [[nodiscard]] constexpr bool rotated() const noexcept { return (v & kRotatedBit) != 0; }

    friend constexpr bool operator==(ResWord, ResWord) = default;
};

}
