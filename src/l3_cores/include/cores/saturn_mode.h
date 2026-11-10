// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "cores/saturn_slot_ring.h"

namespace mister::cores {

struct SaturnModeWord {

    static constexpr std::uint16_t kSpeedDouble = 0x0101;
    static constexpr std::uint16_t kBoot = 0x0808;
    static constexpr std::uint16_t kCdDa = 0x0202;
    static constexpr std::uint16_t kRing = 0x0404;

    static constexpr std::uint16_t kSlotMask = 0x000F;
    static constexpr unsigned kSlotShift = 4;
    static constexpr unsigned kSecondSlotShift = 12;
    static constexpr std::uint16_t kSlotFields = static_cast<std::uint16_t>(
        (kSlotMask << kSlotShift) | (kSlotMask << kSecondSlotShift)
    );

    std::uint16_t v = 0;

    [[nodiscard]] static constexpr SaturnModeWord speed_double() noexcept {
        return SaturnModeWord{kSpeedDouble};
    }

    [[nodiscard]] static constexpr SaturnModeWord boot() noexcept { return SaturnModeWord{kBoot}; }

    [[nodiscard]] static constexpr SaturnModeWord cdda() noexcept { return SaturnModeWord{kCdDa}; }

    [[nodiscard]] static constexpr SaturnModeWord ring() noexcept { return SaturnModeWord{kRing}; }

    [[nodiscard]] constexpr SaturnModeWord with(SaturnModeWord flag) const noexcept {
        return SaturnModeWord{static_cast<std::uint16_t>(v | flag.v)};
    }

    [[nodiscard]] constexpr SaturnModeWord with_slot(SaturnSlotRing::Slot slot) const noexcept {
        const std::uint16_t nib = slot.mode_nibble();
        const std::uint16_t both = static_cast<std::uint16_t>(
            (nib << kSlotShift) | (nib << kSecondSlotShift)
        );
        return SaturnModeWord{
            static_cast<std::uint16_t>((v & static_cast<std::uint16_t>(~kSlotFields)) | both)
        };
    }

    [[nodiscard]] constexpr std::array<std::uint8_t, sizeof(std::uint16_t)>
    wire_bytes() const noexcept {
        return {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)};
    }

    friend constexpr bool operator==(SaturnModeWord, SaturnModeWord) = default;
};

static_assert(sizeof(SaturnModeWord) == 2);

}
