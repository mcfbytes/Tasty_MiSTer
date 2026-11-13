// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>

#include "hal/types.h"
#include "proto/types.h"

namespace mister::proto {

struct DirWord {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint16_t low16() const noexcept {
        return static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max());
    }

    [[nodiscard]] constexpr std::uint16_t high16() const noexcept {
        return static_cast<std::uint16_t>(v >> std::numeric_limits<std::uint16_t>::digits);
    }

    [[nodiscard]] constexpr std::uint8_t top_byte() const noexcept {
        return static_cast<std::uint8_t>(
            v
            >> (std::numeric_limits<std::uint32_t>::digits
                - std::numeric_limits<std::uint8_t>::digits)
        );
    }

    [[nodiscard]] constexpr std::uint8_t flags_byte() const noexcept {
        return static_cast<std::uint8_t>(
            (v >> std::numeric_limits<std::uint16_t>::digits)
            & std::numeric_limits<std::uint8_t>::max()
        );
    }

    [[nodiscard]] constexpr bool flags_bit(std::uint8_t bit) const noexcept {
        return (flags_byte() & bit) != 0;
    }

    [[nodiscard]] constexpr hal::LwOffset lw_offset() const noexcept { return hal::LwOffset{v}; }

    [[nodiscard]] constexpr LaneCaps caps() const noexcept { return LaneCaps{v}; }

    [[nodiscard]] constexpr OsdGeometry osd_geometry() const noexcept { return OsdGeometry{v}; }

    [[nodiscard]] constexpr os::PhysAddr phys_addr() const noexcept { return os::PhysAddr{v}; }

    [[nodiscard]] constexpr os::MapLength map_length() const noexcept { return os::MapLength{v}; }

    [[nodiscard]] constexpr IrqOrdinal irq_ordinal() const noexcept { return IrqOrdinal{v}; }

    [[nodiscard]] constexpr CauseTag cause_tag() const noexcept { return CauseTag{v}; }

    [[nodiscard]] constexpr CauseBits cause_bits() const noexcept { return CauseBits{v}; }
};

}
