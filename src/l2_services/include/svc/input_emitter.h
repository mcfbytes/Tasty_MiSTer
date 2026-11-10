// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "hal/reg_block.h"
#include "proto/axi_block_decl.h"
#include "proto/axi_pad_encoder.h"
#include "proto/frame_pad_encoder.h"
#include "proto/held_pad_encoder.h"
#include "proto/joystick_port.h"
#include "proto/link_frame.h"
#include "svc/axis_cal.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

struct PadGates {
    bool grabbed = false;
    bool osd_visible = false;
};

class InputEmitter {
    TASTY_SEAT_RESIDENT(RT);

public:
    InputEmitter() noexcept = default;

    InputEmitter(const InputEmitter&) = delete;
    InputEmitter& operator=(const InputEmitter&) = delete;

    void set_gates(PadGates gates) noexcept { gates_ = gates; }

    bool grabbed() const noexcept { return gates_.grabbed; }

    bool osd_visible() const noexcept { return gates_.osd_visible; }

    void set_joy_mask(PlayerIndex player, JoyMask mask) noexcept;
    JoyMask joy_mask(PlayerIndex player) const noexcept;
    void set_autofire_mask(PlayerIndex player, JoyMask mask) noexcept;
    JoyMask autofire_mask(PlayerIndex player) const noexcept;

    proto::JoystickPort& joysticks() noexcept { return joy_; }

    const proto::JoystickPort& joysticks() const noexcept { return joy_; }

    void hold_pads() noexcept;

    void bind_pads(proto::ILinkFrame& frame) noexcept;
    void bind_pads(
        proto::ILinkFrame& frame,
        hal::reg::Block pad,
        proto::AxiBlockDecl block
    ) noexcept;

    [[nodiscard]] bool pads_on_lw() const noexcept { return lw_.has_value(); }

    [[nodiscard]] bool pads_held() const noexcept { return held_.has_value(); }

    void trust_pads(bool ready) noexcept {
        if (lw_)
            lw_->trust(ready);
    }

    [[nodiscard]] std::uint32_t pad_facts_held() const noexcept;

    [[nodiscard]] std::uint32_t link_words_bound(PlayerIndex player) const noexcept;

    [[nodiscard]] Ex<bool> submit(
        proto::ILinkFrame& frame,
        PlayerIndex player,
        JoyMask buttons,
        JoyMask autofire
    );
    [[nodiscard]] Ex<bool> release(proto::ILinkFrame& frame, PlayerIndex player, JoyMask composed);

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
        if (lw_)
            return *lw_;
        if (held_)
            return *held_;
        return framed_;
    }

    [[nodiscard]] const proto::ILinkPadEncoder& route() const noexcept {
        if (lw_)
            return *lw_;
        if (held_)
            return *held_;
        return framed_;
    }

    void release_held_(proto::ILinkPadEncoder& to, proto::ILinkFrame& frame) noexcept;

    proto::JoystickPort joy_{};

    proto::FramePadEncoder framed_{joy_};
    std::optional<proto::HeldPadEncoder> held_{};
    proto::AxiBlockDecl pad_block_{};
    std::optional<proto::AxiPadEncoder> lw_{};

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

        [[nodiscard]] JoyMask composed(bool grabbed) const noexcept;

        [[nodiscard]] bool zero_armed() const noexcept { return zero_armed_; }

        void disarm() noexcept { zero_armed_ = false; }

    private:
        JoyMask joy_{};
        JoyMask autofire_{};
        bool zero_armed_ = true;
    };

    infra::IndexedArray<PlayerMasks, kMaxPlayers, PlayerIndex> masks_{};
    PadGates gates_{};
    unsigned axis_range_zero_ = 0;
};

}
