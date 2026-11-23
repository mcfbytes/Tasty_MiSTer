// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "proto/build_generation.h"
#include "proto/link/frame_pad_encoder.h"
#include "proto/link/held_pad_encoder.h"
#include "proto/link/joystick_port.h"
#include "proto/link/pad_relay.h"
#include "proto/link/relay_pad_encoder.h"
#include "proto/link_frame.h"
#include "svc/input/axis_cal.h"
#include "svc/types.h"
#include "infra/seat.h"
#include "infra/tally.h"

namespace mister::svc::input {

class InputEmitter {
    TASTY_SEAT_RESIDENT(RT);

public:
    InputEmitter() noexcept = default;

    InputEmitter(const InputEmitter&) = delete;
    InputEmitter& operator=(const InputEmitter&) = delete;

    void set_joy_mask(PlayerIndex player, JoyMask mask) noexcept;
    JoyMask joy_mask(PlayerIndex player) const noexcept;
    void set_autofire_mask(PlayerIndex player, JoyMask mask) noexcept;
    JoyMask autofire_mask(PlayerIndex player) const noexcept;

    proto::link::JoystickPort& joysticks() noexcept { return joy_; }

    const proto::link::JoystickPort& joysticks() const noexcept { return joy_; }

    void hold_pads() noexcept;

    void bind_pads(proto::ILinkFrame& frame) noexcept;

    void bind_relay(proto::BuildGeneration gen) noexcept;

    [[nodiscard]] bool pads_relayed() const noexcept { return relay_.has_value(); }

    [[nodiscard]] bool pads_held() const noexcept { return held_.has_value(); }

    void gate_pads(bool gated) noexcept;

    [[nodiscard]] std::optional<proto::link::PadRelay> take_relay() noexcept;

    [[nodiscard]] std::uint32_t pad_facts_held() const noexcept;

    [[nodiscard]] std::uint32_t link_words_bound(PlayerIndex player) const noexcept;

    [[nodiscard]] Ex<bool> submit(
        proto::ILinkFrame& frame,
        PlayerIndex player,
        proto::PadMasks masks
    );
    [[nodiscard]] Ex<bool> release(proto::ILinkFrame& frame, PlayerIndex player, JoyMask composed);

    [[nodiscard]] Ex<void> push_stick(
        proto::ILinkFrame& frame,
        PlayerIndex player,
        proto::StickSide side,
        proto::StickPos pos
    );

    [[nodiscard]] Ex<unsigned> service_joysticks(proto::ILinkFrame& frame, bool local_walk = true);
    [[nodiscard]] Ex<bool> push_paddle(
        proto::ILinkFrame& frame,
        PlayerIndex player,
        std::int32_t raw,
        const AxisCal& cal
    );
    [[nodiscard]] Ex<bool> push_spinner(
        proto::ILinkFrame& frame,
        PlayerIndex player,
        std::int32_t delta
    );

    unsigned axis_range_zero() const noexcept { return axis_range_zero_; }

private:
    [[nodiscard]] proto::ILinkPadEncoder& route() noexcept {
        if (relay_)
            return *relay_;
        if (held_)
            return *held_;
        return framed_;
    }

    [[nodiscard]] const proto::ILinkPadEncoder& route() const noexcept {
        if (relay_)
            return *relay_;
        if (held_)
            return *held_;
        return framed_;
    }

    void release_held_(proto::ILinkPadEncoder& to, proto::ILinkFrame& frame) noexcept;

    proto::link::JoystickPort joy_{};

    proto::link::FramePadEncoder framed_{joy_};
    std::optional<proto::link::HeldPadEncoder> held_{};

    std::optional<proto::link::RelayPadEncoder> relay_{};
    bool gated_ = false;
    infra::Tally relay_seq_{};

    class PlayerMasks {
        TASTY_SEAT_RESIDENT(RT);

    public:
        [[nodiscard]] JoyMask joy() const noexcept { return joy_; }

        [[nodiscard]] JoyMask autofire() const noexcept { return autofire_; }

        void set_joy(JoyMask mask) noexcept {
            if (!(mask == joy_))
                zero_armed_ = true;
            joy_ = mask;
        }

        void set_autofire(JoyMask mask) noexcept {
            if (!(mask == autofire_))
                zero_armed_ = true;
            autofire_ = mask;
        }

        [[nodiscard]] JoyMask composed() const noexcept;

        [[nodiscard]] proto::PadMasks pad() const noexcept;

        [[nodiscard]] bool zero_armed() const noexcept { return zero_armed_; }

        void disarm() noexcept { zero_armed_ = false; }

    private:
        JoyMask joy_{};
        JoyMask autofire_{};
        bool zero_armed_ = true;
    };

    infra::IndexedArray<PlayerMasks, kMaxPlayers, PlayerIndex> masks_{};
    unsigned axis_range_zero_ = 0;
};

}
