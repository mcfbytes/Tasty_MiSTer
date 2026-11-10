// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/rumble_channel.h"

namespace mister::svc {

RumbleChannel RumbleChannel::probe(const os::Evdev& ev) noexcept {
    RumbleChannel channel;

    if (!ev.effect_count())
        return channel;
    channel.capable_ = ev.has_ff(FF_RUMBLE);
    return channel;
}

ff_effect make_rumble_effect(
    QuirkId quirk,
    os::FfEffectId id,
    proto::user_io::RumbleMotors motors
) noexcept {
    ff_effect e{};
    e.type = FF_RUMBLE;
    id.stamp_into(e);
    e.direction = quirk == QuirkId::Wheel ? 0x4000 : 0x0000;
    e.u.rumble.strong_magnitude = motors.strong;
    e.u.rumble.weak_magnitude = motors.weak;
    e.replay.length = kRumbleLengthMs;
    e.replay.delay = 0;
    return e;
}

Ex<void> RumbleChannel::play(os::Evdev& ev, QuirkId quirk, proto::user_io::RumbleMotors motors) {
    if (!capable_ || !ev.valid())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (motors.strong == 0 && motors.weak == 0) {
        if (!id_)
            return {};
        const Ex<void> removed = ev.erase_effect(*id_);
        id_.reset();
        if (!removed)
            return removed;
        return {};
    }
    const ff_effect e = make_rumble_effect(quirk, id_.value_or(os::FfEffectId{}), motors);
    const Ex<os::FfEffectId> uploaded = ev.upload_effect(e);
    if (!uploaded)
        return std::unexpected(uploaded.error());
    id_ = *uploaded;
    if (const Ex<void> played = ev.play(*uploaded); !played)
        return played;
    return {};
}

bool RumbleChannel::release(os::Evdev& ev) noexcept {
    if (!id_)
        return true;
    const bool ok = ev.valid() && ev.erase_effect(*id_).has_value();
    id_.reset();
    return ok;
}

}
