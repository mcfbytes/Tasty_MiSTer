// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/joystick.h"
#include "hal/selected.h"

namespace mister::proto {

namespace {

constexpr std::uint16_t kJoystick0 = 0x02;
constexpr std::uint16_t kJoystick2 = 0x10;
constexpr std::uint16_t kAstick = 0x1A;
constexpr std::uint16_t kAstick2 = 0x3D;

Ex<void> put(hal::ISpiTransport& link, std::uint16_t w) {
    auto r = link.transfer(hal::SpiWord{w});
    if (!r) return std::unexpected(r.error());
    return {};
}

}  // namespace

Ex<void> JoystickPort::push_digital(hal::ISpiTransport& link, PlayerIndex player, JoyMask map,
                                    bool new_dir) {
    if (player.v >= kMaxPorts) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }
    const std::uint8_t port = swapped(player.v);

    if ((map.v >> 16) != 0u) use32_ = true;

    const auto opcode =
        static_cast<std::uint16_t>((port < 2) ? (kJoystick0 + port) : (kJoystick2 + port - 2u));

    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        if (auto e = put(link, opcode); !e) return e;
        if (auto e = put(link, static_cast<std::uint16_t>(map.v & 0xFFFFu)); !e) return e;

        if (auto e = put(link, static_cast<std::uint16_t>(map.v >> 16)); !e) return e;
    }

    if (!wire_.suppress_analog_followup && wire_.joy_transl == 1 && new_dir) {
        const auto x = static_cast<std::int8_t>((map.v & 0x2u) ? -128 : ((map.v & 0x1u) ? 127 : 0));
        const auto y = static_cast<std::int8_t>((map.v & 0x8u) ? -128 : ((map.v & 0x4u) ? 127 : 0));
        return push_analog_l(link, stick_index(player), x, y);
    }
    return {};
}

Ex<void> JoystickPort::push_analog(hal::ISpiTransport& link, std::uint16_t opcode,
                                   std::uint8_t index, std::uint8_t x, std::uint8_t y) {

    if (!wire_.analog_capable) return {};

    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto e = put(link, opcode); !e) return e;
    if (auto e = put(link, swapped(index)); !e) return e;

    if (wire_.analog_word) {

        const auto w = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(y) << 8) |
            static_cast<std::uint16_t>(x));
        return put(link, w);
    }

    if (auto e = put(link, x); !e) return e;
    return put(link, y);
}

Ex<void> JoystickPort::push_analog_l(hal::ISpiTransport& link, std::uint8_t index, std::int8_t x,
                                     std::int8_t y) {
    return push_analog(link, kAstick, index, static_cast<std::uint8_t>(x),
                       static_cast<std::uint8_t>(y));
}

Ex<void> JoystickPort::push_analog_r(hal::ISpiTransport& link, std::uint8_t index, std::int8_t x,
                                     std::int8_t y) {

    return push_analog(link, kAstick2, index, static_cast<std::uint8_t>(x),
                       static_cast<std::uint8_t>(y));
}

Ex<void> JoystickPort::push_paddle(hal::ISpiTransport& link, PlayerIndex player,
                                   std::uint8_t value) {
    if (player.v >= kMaxPorts) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }
    return push_analog(link, kAstick, paddle_index(player), value, 0);
}

Ex<void> JoystickPort::push_spinner(hal::ISpiTransport& link, PlayerIndex player,
                                    std::int8_t delta) {
    if (player.v >= kMaxPorts) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }

    return push_analog(link, kAstick, spinner_index(player), static_cast<std::uint8_t>(delta), 0);
}

Ex<bool> JoystickPort::submit(hal::ISpiTransport& link, PlayerIndex player, JoyMask buttons,
                              JoyMask autofire) {
    if (player.v >= kMaxPorts) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }
    const JoyMask composed{buttons.v | autofire.v};

    const bool new_dir = ((composed.v & 0xFu) | (prev_[player.v].v & 0xFu)) != 0u;

    if (composed == prev_[player.v]) return false;

    if (auto e = push_digital(link, player, composed, new_dir); !e) {
        return std::unexpected(e.error());
    }
    prev_[player.v] = composed;
    return true;
}

Ex<bool> JoystickPort::release(hal::ISpiTransport& link, PlayerIndex player, JoyMask composed) {
    if (player.v >= kMaxPorts) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }

    if (composed.v == 0u) return false;
    if (auto e = push_digital(link, player, JoyMask{0}, true); !e) {
        return std::unexpected(e.error());
    }
    return true;
}

Ex<std::uint8_t> JoystickPort::paddle_scale(std::int32_t raw, std::int32_t min, std::int32_t max) {

    if (max == min) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(min)});
    }
    std::int32_t value = raw;
    if (value < min)
        value = min;
    else if (value > max)
        value = max;

    const std::int64_t num = static_cast<std::int64_t>(value - min) * 255;
    const std::int64_t den = static_cast<std::int64_t>(max) - static_cast<std::int64_t>(min);
    const std::int64_t scaled = num / den;

    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(scaled) & 0xFFu);
}

}  // namespace mister::proto
