// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "proto/joy_bits.h"
#include "proto/link/pad_ports.h"
#include "proto/link_pad_encoder.h"
#include "proto/pad_masks.h"
#include "proto/stick_pos.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto::link {

class JoystickPort {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::uint8_t kMaxPorts = kPadPorts;
    static constexpr std::array<UioOp, kMaxPorts> kDigitalOpcode = kPadDigitalOpcode;

    static constexpr std::uint32_t kMaxDigitalPushWords = 3 + 4;

    void configure(const JoystickWire& w) noexcept { wire_ = w; }

    const JoystickWire& wire() const noexcept { return wire_; }

    [[nodiscard]] Ex<void> push_digital(
        ILinkPadEncoder& pads,
        PlayerIndex player,
        JoyMask map,
        bool new_dir
    );

    [[nodiscard]] Ex<void> push_analog_l(ILinkPadEncoder& pads, std::uint8_t index, StickPos pos);
    [[nodiscard]] Ex<void> push_analog_r(ILinkPadEncoder& pads, std::uint8_t index, StickPos pos);

    [[nodiscard]] Ex<void> push_stick(
        ILinkPadEncoder& pads,
        PlayerIndex player,
        StickSide side,
        StickPos pos
    );

    [[nodiscard]] Ex<void> push_paddle(
        ILinkPadEncoder& pads,
        PlayerIndex player,
        std::uint8_t value
    );

    [[nodiscard]] Ex<void> push_spinner(
        ILinkPadEncoder& pads,
        PlayerIndex player,
        std::int8_t delta
    );

    [[nodiscard]] Ex<bool> submit(ILinkPadEncoder& pads, PlayerIndex player, PadMasks masks);

    [[nodiscard]] Ex<bool> release(ILinkPadEncoder& pads, PlayerIndex player, JoyMask composed);

    JoyMask previous(PlayerIndex player) const noexcept {
        if (const auto mask = prev_.find(player))
            return *mask;
        return JoyMask{};
    }

    void reset_edges() noexcept {
        for (JoyMask& m : prev_)
            m = JoyMask{};
    }

    static constexpr std::uint8_t stick_index(PlayerIndex p) noexcept {
        return p.stick_wire_index();
    }

    static constexpr std::uint8_t paddle_index(PlayerIndex p) noexcept {
        return p.paddle_wire_index();
    }

    static constexpr std::uint8_t spinner_index(PlayerIndex p) noexcept {
        return p.spinner_wire_index();
    }

    [[nodiscard]] static Ex<std::uint8_t> paddle_scale(
        std::int32_t raw,
        std::int32_t min,
        std::int32_t max
    );

    static constexpr std::int8_t spinner_clamp(std::int32_t delta) noexcept {
        constexpr std::int32_t lo = std::numeric_limits<std::int8_t>::min();
        constexpr std::int32_t hi = std::numeric_limits<std::int8_t>::max();
        if (delta < lo)
            return std::numeric_limits<std::int8_t>::min();
        if (delta > hi)
            return std::numeric_limits<std::int8_t>::max();
        return static_cast<std::int8_t>(delta);
    }

    PlayerIndex wire_port(PlayerIndex player) const noexcept {
        return wire_.joyswap ? player.joyswapped() : player;
    }

    static constexpr UioOp digital_opcode(PlayerIndex port) noexcept {
        return link::digital_opcode(port);
    }

private:
    std::uint8_t swapped(std::uint8_t port) const noexcept {
        if (port > 1u || !wire_.joyswap)
            return port;
        return static_cast<std::uint8_t>(port ^ 1u);
    }

    [[nodiscard]] Ex<void> push_analog(
        ILinkPadEncoder& pads,
        UioOp opcode,
        std::uint8_t index,
        StickPos pos
    );

    JoystickWire wire_{};

    infra::IndexedArray<JoyMask, kMaxPorts, PlayerIndex> prev_{};
};

}
