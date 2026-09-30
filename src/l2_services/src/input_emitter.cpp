// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_emitter.h"
#include "hal/selected.h"

#include "svc/input_service.h"

namespace mister::svc {

static_assert(kMaxPlayersEmit == kMaxPlayers,
              "InputEmitter's player table must match svc::kMaxPlayers");
static_assert(kMaxPlayersEmit <= proto::JoystickPort::kMaxPorts,
              "svc::kMaxPlayers exceeds the UIO_JOYSTICK port space (NUMPLAYERS)");

void InputEmitter::set_joy_mask(PlayerIndex player, JoyMask mask) noexcept {
    if (player.v >= kMaxPlayersEmit) return;

    if (!(mask == joy_mask_[player.v])) zero_armed_[player.v] = true;
    joy_mask_[player.v] = mask;
}

JoyMask InputEmitter::joy_mask(PlayerIndex player) const noexcept {
    if (player.v >= kMaxPlayersEmit) return JoyMask{};
    return joy_mask_[player.v];
}

void InputEmitter::set_autofire_mask(PlayerIndex player, JoyMask mask) noexcept {
    if (player.v >= kMaxPlayersEmit) return;

    if (!(mask == autofire_[player.v])) zero_armed_[player.v] = true;
    autofire_[player.v] = mask;
}

JoyMask InputEmitter::autofire_mask(PlayerIndex player) const noexcept {
    if (player.v >= kMaxPlayersEmit) return JoyMask{};
    return autofire_[player.v];
}

Ex<unsigned> InputEmitter::service_joysticks(hal::ISpiTransport& link, bool local_walk) {
    unsigned pushed = 0;

    if (local_walk && grabbed_) {
        for (std::uint8_t i = 0; i < kMaxPlayersEmit; ++i) {
            const PlayerIndex p{i};
            auto sent = joy_.submit(link, p, joy_mask_[i], autofire_[i]);
            if (!sent) return std::unexpected(sent.error());
            if (*sent) ++pushed;
        }
    }

    if (local_walk && (!grabbed_ || osd_visible_)) {
        for (std::uint8_t i = 0; i < kMaxPlayersEmit; ++i) {
            const PlayerIndex p{i};

            const JoyMask tested =
                grabbed_ ? JoyMask{joy_mask_[i].v | autofire_[i].v} : joy_mask_[i];
            if (!zero_armed_[i]) continue;
            auto sent = joy_.release(link, p, tested);
            if (!sent) return std::unexpected(sent.error());

            zero_armed_[i] = false;
            if (*sent) ++pushed;
        }
    }
    return pushed;
}

Ex<bool> InputEmitter::push_paddle(hal::ISpiTransport& link, PlayerIndex player, std::int32_t raw,
                                   const AxisCal& cal) {
    if (player.v >= kMaxPlayersEmit) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }

    if (osd_visible_) return false;

    auto byte = proto::JoystickPort::paddle_scale(raw, cal.min, cal.max);
    if (!byte) {

        ++axis_range_zero_;
        return std::unexpected(byte.error());
    }
    if (auto r = joy_.push_paddle(link, player, *byte); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

Ex<bool> InputEmitter::push_spinner(hal::ISpiTransport& link, PlayerIndex player,
                                    std::int32_t delta) {
    if (player.v >= kMaxPlayersEmit) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }
    if (osd_visible_) return false;
    if (auto r = joy_.push_spinner(link, player, proto::JoystickPort::spinner_clamp(delta)); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

}  // namespace mister::svc
