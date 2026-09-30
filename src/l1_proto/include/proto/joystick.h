// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/types.h"
#include "infra/seat.h"

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

static_assert(kJoyDown == 0x04 && kJoyUp == 0x08,
              "video-rule: user_io.h:105-106 — DOWN is the LOWER bit. Transposing "
              "them moves the menu cursor the wrong way with no diagnostic "
              "anywhere, which is why this is a build error and not a comment.");
static_assert(kJoyBtn3 == (kJoyBtn1 << 2),
              "video-rule: the BTN1+BTN2 chord SYNTHESISES BTN3 (input.cpp:2424); "
              "the three bits must stay in this relation");

struct JoystickWire {
    bool analog_capable = false;
    bool analog_word = false;
    bool joyswap = false;

    bool suppress_analog_followup = false;
    std::uint8_t joy_transl = 0;

    friend constexpr bool operator==(const JoystickWire&, const JoystickWire&) = default;
};

class JoystickPort {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::uint8_t kMaxPorts = 6;

    static constexpr std::uint32_t kMaxDigitalPushWords = 3 + 4;

    void configure(const JoystickWire& w) noexcept { wire_ = w; }
    const JoystickWire& wire() const noexcept { return wire_; }

    Ex<void> push_digital(hal::ISpiTransport& link, PlayerIndex player, JoyMask map, bool new_dir);

    Ex<void> push_analog_l(hal::ISpiTransport& link, std::uint8_t index, std::int8_t x,
                           std::int8_t y);
    Ex<void> push_analog_r(hal::ISpiTransport& link, std::uint8_t index, std::int8_t x,
                           std::int8_t y);

    Ex<void> push_paddle(hal::ISpiTransport& link, PlayerIndex player, std::uint8_t value);

    Ex<void> push_spinner(hal::ISpiTransport& link, PlayerIndex player, std::int8_t delta);

    Ex<bool> submit(hal::ISpiTransport& link, PlayerIndex player, JoyMask buttons,
                    JoyMask autofire);

    Ex<bool> release(hal::ISpiTransport& link, PlayerIndex player, JoyMask composed);

    JoyMask previous(PlayerIndex player) const noexcept {
        return (player.v < kMaxPorts) ? prev_[player.v] : JoyMask{};
    }

    void reset_edges() noexcept {
        for (JoyMask& m : prev_)
            m = JoyMask{};
    }

    static constexpr std::uint8_t stick_index(PlayerIndex p) noexcept { return p.v; }
    static constexpr std::uint8_t paddle_index(PlayerIndex p) noexcept {
        return static_cast<std::uint8_t>((static_cast<unsigned>(p.v) << 4u) | 0x0Fu);
    }
    static constexpr std::uint8_t spinner_index(PlayerIndex p) noexcept {
        return static_cast<std::uint8_t>((static_cast<unsigned>(p.v) << 4u) | 0x8Fu);
    }

    static Ex<std::uint8_t> paddle_scale(std::int32_t raw, std::int32_t min, std::int32_t max);

    static constexpr std::int8_t spinner_clamp(std::int32_t delta) noexcept {
        if (delta < -128) return static_cast<std::int8_t>(-128);
        if (delta > 127) return static_cast<std::int8_t>(127);
        return static_cast<std::int8_t>(delta);
    }

    PlayerIndex wire_port(PlayerIndex player) const noexcept {
        return PlayerIndex{swapped(player.v)};
    }

    bool wide() const noexcept { return use32_; }

private:
    std::uint8_t swapped(std::uint8_t port) const noexcept {
        if (port > 1u || !wire_.joyswap) return port;
        return static_cast<std::uint8_t>(port ^ 1u);
    }

    Ex<void> push_analog(hal::ISpiTransport& link, std::uint16_t opcode, std::uint8_t index,
                         std::uint8_t x, std::uint8_t y);

    JoystickWire wire_{};

    bool use32_ = false;

    JoyMask prev_[kMaxPorts]{};
};

}  // namespace mister::proto
