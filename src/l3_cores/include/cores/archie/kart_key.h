// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cores/archie/kart_byte.h"

namespace mister::cores::archie {

struct KartKey {
    static constexpr unsigned kNibbleBits = 4;
    static constexpr std::uint8_t kNibble = 0x0F;

    std::uint8_t v = 0;

    [[nodiscard]] constexpr KartByte row() const noexcept {
        return KartByte{static_cast<std::uint8_t>(v >> kNibbleBits)};
    }

    [[nodiscard]] constexpr KartByte column() const noexcept {
        return KartByte{static_cast<std::uint8_t>(v & kNibble)};
    }

    [[nodiscard]] constexpr std::array<KartByte, 2> event(KartByte prefix) const noexcept {
        return {prefix.with(row()), prefix.with(column())};
    }

    friend constexpr bool operator==(KartKey, KartKey) = default;
};

inline constexpr KartKey kKartEscape{0x00};
inline constexpr KartKey kKartKey1{0x11};
inline constexpr KartKey kKartKey2{0x12};
inline constexpr KartKey kKartKey3{0x13};
inline constexpr KartKey kKartBreak{0x0F};
inline constexpr KartKey kKartMouseExtra1{0x73};
inline constexpr KartKey kKartMouseExtra2{0x74};
inline constexpr KartKey kKartKeypadHash{0x25};

inline constexpr std::array kKartMouseButtons{KartKey{0x70}, KartKey{0x72}, KartKey{0x71}};
inline constexpr std::array kKartMouseButtonsHostOrder{KartKey{0x70}, KartKey{0x71}, KartKey{0x72}};

}
