// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "hal/reg_block.h"
#include "proto/axi_block_decl.h"
#include "proto/axi_pad_encoder.h"
#include "proto/frame_pad_encoder.h"
#include "proto/held_pad_encoder.h"
#include "proto/joystick.h"
#include "proto/link_frame.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

inline constexpr unsigned kMaxPlayersEmit = 6;

struct AxisCal;

class InputEmitter {
    TASTY_SEAT_RESIDENT(RT);

public:
    InputEmitter() noexcept = default;

    InputEmitter(const InputEmitter&) = delete;
    InputEmitter& operator=(const InputEmitter&) = delete;

    void set_gates(bool grabbed, bool osd_visible) noexcept {
        grabbed_ = grabbed;
        osd_visible_ = osd_visible;
    }

    bool grabbed() const noexcept { return grabbed_; }

    bool osd_visible() const noexcept { return osd_visible_; }

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
    void release_held_(proto::ILinkFrame& frame) noexcept;

    proto::JoystickPort joy_{};

    proto::FramePadEncoder framed_{joy_};
    std::optional<proto::HeldPadEncoder> held_{};
    proto::AxiBlockDecl pad_block_{};
    std::optional<proto::AxiPadEncoder> lw_{};
    proto::ILinkPadEncoder* route_ = &framed_;
    JoyMask joy_mask_[kMaxPlayersEmit]{};
    JoyMask autofire_[kMaxPlayersEmit]{};

    bool zero_armed_[kMaxPlayersEmit]{true, true, true, true, true, true};
    bool grabbed_ = false;
    bool osd_visible_ = false;
    unsigned axis_range_zero_ = 0;
};

}
