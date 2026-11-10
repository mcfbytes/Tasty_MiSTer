// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct StickPos {
    std::uint8_t x = 0;
    std::uint8_t y = 0;

    [[nodiscard]] constexpr std::uint16_t packed() const noexcept {
        constexpr unsigned kHighByteShift = 8;
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(y) << kHighByteShift) | x);
    }

    [[nodiscard]] static constexpr StickPos from_packed(std::uint32_t word) noexcept {
        constexpr std::uint32_t kHighByteShift = 8;
        return StickPos{
            static_cast<std::uint8_t>(word),
            static_cast<std::uint8_t>(word >> kHighByteShift)
        };
    }

    friend constexpr bool operator==(StickPos, StickPos) = default;
};

static_assert(StickPos{0x12, 0x34}.packed() == 0x3412, "x is the low byte, y the high one");
static_assert(StickPos::from_packed(0x3412) == StickPos{0x12, 0x34}, "the inverse of packed()");

}
