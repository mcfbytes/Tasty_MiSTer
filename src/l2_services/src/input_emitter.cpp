// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_emitter.h"

#include <utility>

#include "svc/axis_cal.h"

namespace mister::svc {

static_assert(
    kMaxPlayers <= proto::JoystickPort::kMaxPorts,
    "svc::kMaxPlayers exceeds the UIO_JOYSTICK port space (NUMPLAYERS)"
);

void InputEmitter::hold_pads() noexcept {
    held_.emplace();
    lw_.reset();
}

void InputEmitter::bind_pads(proto::ILinkFrame& frame) noexcept {
    if (!held_)
        hold_pads();
    release_held_(framed_, frame);
}

void InputEmitter::bind_pads(
    proto::ILinkFrame& frame,
    hal::reg::Block pad,
    proto::PadBlockDecl block
) noexcept {
    if (!held_)
        hold_pads();
    pad_block_ = block;
    lw_.emplace(std::move(pad), pad_block_, framed_);
    release_held_(*lw_, frame);
}

void InputEmitter::release_held_(proto::ILinkPadEncoder& to, proto::ILinkFrame& frame) noexcept {
    framed_.over(frame);
    (void)held_->release(to);
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
    return route().link_words(proto::JoystickPort::digital_opcode(port), follow_up);
}

Ex<bool> InputEmitter::submit(
    proto::ILinkFrame& frame,
    PlayerIndex player,
    JoyMask buttons,
    JoyMask autofire
) {
    framed_.over(frame);
    return joy_.submit(route(), player, buttons, autofire);
}

Ex<bool> InputEmitter::release(proto::ILinkFrame& frame, PlayerIndex player, JoyMask composed) {
    framed_.over(frame);
    return joy_.release(route(), player, composed);
}

JoyMask InputEmitter::PlayerMasks::composed() const noexcept {
    return joy_.with(autofire_);
}

void InputEmitter::set_joy_mask(PlayerIndex player, JoyMask mask) noexcept {
    if (const auto m = masks_.find(player))
        m->set_joy(mask);
}

JoyMask InputEmitter::joy_mask(PlayerIndex player) const noexcept {
    const auto m = masks_.find(player);
    return m ? m->joy() : JoyMask{};
}

void InputEmitter::set_autofire_mask(PlayerIndex player, JoyMask mask) noexcept {
    if (const auto m = masks_.find(player))
        m->set_autofire(mask);
}

JoyMask InputEmitter::autofire_mask(PlayerIndex player) const noexcept {
    const auto m = masks_.find(player);
    return m ? m->autofire() : JoyMask{};
}

Ex<unsigned> InputEmitter::service_joysticks(proto::ILinkFrame& frame, bool local_walk) {
    unsigned pushed = 0;

    if (local_walk) {
        for (const PlayerIndex p : kPlayers) {
            auto sent = submit(frame, p, masks_[p].joy(), masks_[p].autofire());
            if (!sent)
                return std::unexpected(sent.error());
            if (*sent)
                ++pushed;
        }
    }

    if (local_walk) {
        for (const PlayerIndex p : kPlayers) {
            PlayerMasks& masks = masks_[p];
            if (!masks.zero_armed())
                continue;

            auto sent = release(frame, p, masks.composed());
            if (!sent)
                return std::unexpected(sent.error());

            masks.disarm();
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
    if (player.v >= kMaxPlayers) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }

    auto byte = proto::JoystickPort::paddle_scale(raw, cal.min, cal.max);
    if (!byte) {

        ++axis_range_zero_;
        return std::unexpected(byte.error());
    }
    framed_.over(frame);
    if (const Ex<void> r = joy_.push_paddle(route(), player, *byte); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

Ex<bool> InputEmitter::push_spinner(
    proto::ILinkFrame& frame,
    PlayerIndex player,
    std::int32_t delta
) {
    if (player.v >= kMaxPlayers) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }
    const std::int8_t clamped = proto::JoystickPort::spinner_clamp(delta);
    framed_.over(frame);
    if (const Ex<void> r = joy_.push_spinner(route(), player, clamped); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

}
