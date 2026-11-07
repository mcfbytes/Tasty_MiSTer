// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/joystick.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

namespace {

constexpr JoyMask kDirections{kJoyRight | kJoyLeft | kJoyDown | kJoyUp};

std::unexpected<Error> bad_player(PlayerIndex player, infra::ErrorSite site) noexcept {
    return std::unexpected(Error{Errc::slot_range, site, player.v});
}

}

Ex<void> JoystickPort::push_digital(
    ILinkPadEncoder& pads,
    PlayerIndex player,
    JoyMask map,
    bool new_dir
) {
    if (!prev_.find(player))
        return bad_player(player, ERR_SITE());
    const std::uint8_t port = swapped(player.v);

    if (map.beyond_low_half())
        use32_ = true;

    const UioOp opcode = digital_opcode(port);

    if (auto e = pads.digital(opcode, map); !e)
        return e;

    if (!wire_.suppress_analog_followup && wire_.joy_transl == 1 && new_dir) {
        const std::uint8_t x = map.any(JoyMask{kJoyLeft})
            ? 0x80
            : (map.any(JoyMask{kJoyRight}) ? 0x7F : 0);
        const std::uint8_t y = map.any(JoyMask{kJoyUp}) ? 0x80
                                                        : (map.any(JoyMask{kJoyDown}) ? 0x7F : 0);
        return push_analog_l(pads, stick_index(player), StickPos{x, y});
    }
    return {};
}

Ex<void> JoystickPort::push_analog(
    ILinkPadEncoder& pads,
    UioOp opcode,
    std::uint8_t index,
    StickPos pos
) {

    if (!wire_.analog_capable)
        return {};

    return pads.stick(opcode, swapped(index), pos);
}

Ex<void> JoystickPort::push_analog_l(ILinkPadEncoder& pads, std::uint8_t index, StickPos pos) {
    return push_analog(pads, UioOp::Astick, index, pos);
}

Ex<void> JoystickPort::push_analog_r(ILinkPadEncoder& pads, std::uint8_t index, StickPos pos) {

    return push_analog(pads, UioOp::Astick2, index, pos);
}

Ex<void> JoystickPort::push_paddle(ILinkPadEncoder& pads, PlayerIndex player, std::uint8_t value) {
    if (!prev_.find(player))
        return bad_player(player, ERR_SITE());
    return push_analog(pads, UioOp::Astick, paddle_index(player), StickPos{value, 0});
}

Ex<void> JoystickPort::push_spinner(ILinkPadEncoder& pads, PlayerIndex player, std::int8_t delta) {
    if (!prev_.find(player))
        return bad_player(player, ERR_SITE());

    return push_analog(
        pads,
        UioOp::Astick,
        spinner_index(player),
        StickPos{static_cast<std::uint8_t>(delta), 0}
    );
}

Ex<bool> JoystickPort::submit(
    ILinkPadEncoder& pads,
    PlayerIndex player,
    JoyMask buttons,
    JoyMask autofire
) {
    if (!prev_.find(player))
        return bad_player(player, ERR_SITE());
    const JoyMask composed = buttons.with(autofire);

    const bool new_dir = composed.with(prev_[player]).any(kDirections);

    if (composed == prev_[player])
        return false;

    if (auto e = push_digital(pads, player, composed, new_dir); !e) {
        return std::unexpected(e.error());
    }
    prev_[player] = composed;
    return true;
}

Ex<bool> JoystickPort::release(ILinkPadEncoder& pads, PlayerIndex player, JoyMask composed) {
    if (!prev_.find(player))
        return bad_player(player, ERR_SITE());

    if (composed == JoyMask{})
        return false;
    if (auto e = push_digital(pads, player, JoyMask{0}, true); !e) {
        return std::unexpected(e.error());
    }
    return true;
}

Ex<std::uint8_t> JoystickPort::paddle_scale(std::int32_t raw, std::int32_t min, std::int32_t max) {

    if (max == min) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(min)}
        );
    }
    std::int32_t value = raw;
    if (value < min)
        value = min;
    else if (value > max)
        value = max;

    const std::int64_t num = std::int64_t{value - min} * 255;
    const std::int64_t den = std::int64_t{max} - min;
    const std::int64_t scaled = num / den;

    return static_cast<std::uint8_t>(scaled);
}

}
