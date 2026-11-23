// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input/input_emitter.h"

#include <utility>

#include "infra/prelude.h"
#include "svc/input/axis_cal.h"

namespace mister::svc::input {

static_assert(
    kMaxPlayers <= proto::link::JoystickPort::kMaxPorts,
    "svc::kMaxPlayers exceeds the UIO_JOYSTICK port space (NUMPLAYERS)"
);

void InputEmitter::hold_pads() noexcept {
    held_.emplace();
    relay_.reset();
}

void InputEmitter::bind_pads(proto::ILinkFrame& frame) noexcept {
    if (!held_)
        hold_pads();
    release_held_(framed_, frame);
}

void InputEmitter::bind_relay(proto::BuildGeneration gen) noexcept {
    held_.reset();
    relay_.emplace(gen, gated_, relay_seq_);
}

void InputEmitter::gate_pads(bool gated) noexcept {
    gated_ = gated;
    if (relay_)
        relay_->gate(gated);
}

std::optional<proto::link::PadRelay> InputEmitter::take_relay() noexcept {
    if (!relay_)
        return std::nullopt;
    std::optional<proto::link::PadRelay> moved = relay_->take_moved();
    if (moved)
        relay_seq_ = moved->seq;
    return moved;
}

void InputEmitter::release_held_(proto::ILinkPadEncoder& to, proto::ILinkFrame& frame) noexcept {
    framed_.over(frame);
    (void)held_->release(to);
    framed_.clear_frame();
    held_.reset();
}

std::uint32_t InputEmitter::pad_facts_held() const noexcept {
    return held_ ? held_->held() : 0u;
}

std::uint32_t InputEmitter::link_words_bound(PlayerIndex player) const noexcept {
    const proto::JoystickWire& w = joy_.wire();
    const bool follow_up = w.analog_capable && !w.suppress_analog_followup && w.joy_transl == 1;
    return route().link_words(
        proto::link::JoystickPort::digital_opcode(joy_.wire_port(player)),
        follow_up
    );
}

Ex<bool> InputEmitter::submit(proto::ILinkFrame& frame, PlayerIndex player, proto::PadMasks masks) {
    framed_.over(frame);
    return joy_.submit(route(), player, masks);
}

Ex<bool> InputEmitter::release(proto::ILinkFrame& frame, PlayerIndex player, JoyMask composed) {
    framed_.over(frame);
    return joy_.release(route(), player, composed);
}

Ex<void> InputEmitter::push_stick(
    proto::ILinkFrame& frame,
    PlayerIndex player,
    proto::StickSide side,
    proto::StickPos pos
) {
    framed_.over(frame);
    return joy_.push_stick(route(), player, side, pos);
}

JoyMask InputEmitter::PlayerMasks::composed() const noexcept {
    return joy_.with(autofire_);
}

proto::PadMasks InputEmitter::PlayerMasks::pad() const noexcept {
    return proto::PadMasks{joy_, autofire_};
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
            auto sent = submit(frame, p, masks_[p].pad());
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
    if (!masks_.find(player)) {
        return std::unexpected(player.refusal(Errc::slot_range, ERR_SITE()));
    }

    auto byte = proto::link::JoystickPort::paddle_scale(raw, cal.min, cal.max);
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
    if (!masks_.find(player)) {
        return std::unexpected(player.refusal(Errc::slot_range, ERR_SITE()));
    }
    const std::int8_t clamped = proto::link::JoystickPort::spinner_clamp(delta);
    framed_.over(frame);
    if (const Ex<void> r = joy_.push_spinner(route(), player, clamped); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

}
