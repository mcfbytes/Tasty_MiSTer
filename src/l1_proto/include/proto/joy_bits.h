// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::proto {

inline constexpr std::uint32_t kJoyRight = 0x0001;
inline constexpr std::uint32_t kJoyLeft = 0x0002;
inline constexpr std::uint32_t kJoyDown = 0x0004;
inline constexpr std::uint32_t kJoyUp = 0x0008;
inline constexpr std::uint32_t kJoyBtn1 = 0x0010;
inline constexpr std::uint32_t kJoyBtn2 = 0x0020;
inline constexpr std::uint32_t kJoyBtn3 = 0x0040;
inline constexpr std::uint32_t kJoyBtn4 = 0x0080;
inline constexpr std::uint32_t kJoyX = 0x0100;
inline constexpr std::uint32_t kJoyY = 0x0200;
inline constexpr std::uint32_t kJoyL = 0x0400;
inline constexpr std::uint32_t kJoyR = 0x0800;
inline constexpr std::uint32_t kJoyL2 = 0x1000;
inline constexpr std::uint32_t kJoyR2 = 0x2000;
inline constexpr std::uint32_t kJoyL3 = 0x4000;
inline constexpr std::uint32_t kJoyR3 = 0x8000;
inline constexpr std::uint32_t kJoyMove = kJoyRight | kJoyLeft | kJoyUp | kJoyDown;

static_assert(
    kJoyDown == 0x04 && kJoyUp == 0x08,
    "video-rule: user_io.h:105-106 — DOWN is the LOWER bit. Transposing "
    "them moves the menu cursor the wrong way with no diagnostic "
    "anywhere, which is why this is a build error and not a comment."
);
static_assert(
    kJoyBtn3 == (kJoyBtn1 << 2),
    "video-rule: the BTN1+BTN2 chord SYNTHESISES BTN3 (input.cpp:2424); "
    "the three bits must stay in this relation"
);

struct JoystickWire {
    bool analog_capable = false;
    bool analog_word = false;
    bool joyswap = false;

    bool suppress_analog_followup = false;
    std::uint8_t joy_transl = 0;

    [[nodiscard]] constexpr PlayerIndex port_of(PlayerIndex player) const noexcept {
        return joyswap ? player.joyswapped() : player;
    }

    [[nodiscard]] constexpr bool translates() const noexcept {
        return !suppress_analog_followup && joy_transl == 1;
    }

    friend constexpr bool operator==(const JoystickWire&, const JoystickWire&) = default;
};

}
