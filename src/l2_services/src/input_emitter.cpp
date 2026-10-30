// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_emitter.h"

#include <utility>

#include "svc/input_service.h"

namespace mister::svc {

static_assert(
    kMaxPlayersEmit == kMaxPlayers,
    "InputEmitter's player table must match svc::kMaxPlayers"
);
static_assert(
    kMaxPlayersEmit <= proto::JoystickPort::kMaxPorts,
    "svc::kMaxPlayers exceeds the UIO_JOYSTICK port space (NUMPLAYERS)"
);

void InputEmitter::hold_pads() noexcept {
    held_.emplace();
    route_ = &*held_;
    lw_.reset();
}

void InputEmitter::bind_pads(proto::ILinkFrame& frame) noexcept {
    if (!held_)
        hold_pads();
    route_ = &framed_;
    release_held_(frame);
}

void InputEmitter::bind_pads(
    proto::ILinkFrame& frame,
    hal::reg::Block pad,
    proto::AxiBlockDecl block
) noexcept {
    if (!held_)
        hold_pads();
    pad_block_ = block;
    lw_.emplace(std::move(pad), pad_block_, framed_);
    route_ = &*lw_;
    release_held_(frame);
}

void InputEmitter::release_held_(proto::ILinkFrame& frame) noexcept {
    framed_.over(frame);
    (void)held_->release(*route_);
    framed_.clear_frame();
    held_.reset();
}

std::uint32_t InputEmitter::pad_facts_held() const noexcept {
    if (lw_)
        return lw_->held();
    return held_ ? held_->held() : 0u;
}

std::uint32_t InputEmitter::link_words_bound(PlayerIndex player) const noexcept {
    const std::uint8_t port = joy_.wire_port(player).v;
    const proto::JoystickWire& w = joy_.wire();
    const bool follow_up = w.analog_capable && !w.suppress_analog_followup && w.joy_transl == 1;
    return route_->link_words(proto::JoystickPort::digital_opcode(port), follow_up);
}

Ex<bool> InputEmitter::submit(
    proto::ILinkFrame& frame,
    PlayerIndex player,
    JoyMask buttons,
    JoyMask autofire
) {
    framed_.over(frame);
    return joy_.submit(*route_, player, buttons, autofire);
}

Ex<bool> InputEmitter::release(proto::ILinkFrame& frame, PlayerIndex player, JoyMask composed) {
    framed_.over(frame);
    return joy_.release(*route_, player, composed);
}

void InputEmitter::set_joy_mask(PlayerIndex player, JoyMask mask) noexcept {
    if (player.v >= kMaxPlayersEmit)
        return;

    if (!(mask == joy_mask_[player.v]))
        zero_armed_[player.v] = true;
    joy_mask_[player.v] = mask;
}

JoyMask InputEmitter::joy_mask(PlayerIndex player) const noexcept {
    if (player.v >= kMaxPlayersEmit)
        return JoyMask{};
    return joy_mask_[player.v];
}

void InputEmitter::set_autofire_mask(PlayerIndex player, JoyMask mask) noexcept {
    if (player.v >= kMaxPlayersEmit)
        return;

    if (!(mask == autofire_[player.v]))
        zero_armed_[player.v] = true;
    autofire_[player.v] = mask;
}

JoyMask InputEmitter::autofire_mask(PlayerIndex player) const noexcept {
    if (player.v >= kMaxPlayersEmit)
        return JoyMask{};
    return autofire_[player.v];
}

Ex<unsigned> InputEmitter::service_joysticks(proto::ILinkFrame& frame, bool local_walk) {
    unsigned pushed = 0;

    if (local_walk && grabbed_) {
        for (std::uint8_t i = 0; i < kMaxPlayersEmit; ++i) {
            const PlayerIndex p{i};
            auto sent = submit(frame, p, joy_mask_[i], autofire_[i]);
            if (!sent)
                return std::unexpected(sent.error());
            if (*sent)
                ++pushed;
        }
    }

    if (local_walk && (!grabbed_ || osd_visible_)) {
        for (std::uint8_t i = 0; i < kMaxPlayersEmit; ++i) {
            const PlayerIndex p{i};

            const JoyMask tested = grabbed_ ? JoyMask{joy_mask_[i].v | autofire_[i].v}
                                            : joy_mask_[i];
            if (!zero_armed_[i])
                continue;
            auto sent = release(frame, p, tested);
            if (!sent)
                return std::unexpected(sent.error());

            zero_armed_[i] = false;
            if (*sent)
                ++pushed;
        }
    }
    return pushed;
}

Ex<bool> InputEmitter::push_paddle(
    proto::ILinkFrame& frame,
    PlayerIndex player,
    std::int32_t raw,
    const AxisCal& cal
) {
    if (player.v >= kMaxPlayersEmit) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }

    if (osd_visible_)
        return false;

    auto byte = proto::JoystickPort::paddle_scale(raw, cal.min, cal.max);
    if (!byte) {

        ++axis_range_zero_;
        return std::unexpected(byte.error());
    }
    framed_.over(frame);
    if (const Ex<void> r = joy_.push_paddle(*route_, player, *byte); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

Ex<bool> InputEmitter::push_spinner(
    proto::ILinkFrame& frame,
    PlayerIndex player,
    std::int32_t delta
) {
    if (player.v >= kMaxPlayersEmit) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }
    if (osd_visible_)
        return false;
    const std::int8_t clamped = proto::JoystickPort::spinner_clamp(delta);
    framed_.over(frame);
    if (const Ex<void> r = joy_.push_spinner(*route_, player, clamped); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

}
