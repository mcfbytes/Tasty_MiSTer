// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <linux/input.h>

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/evdev.h"
#include "os/ff_effect_id.h"
#include "proto/user_io/rumble_motors.h"
#include "svc/quirk_id.h"

namespace mister::svc {

inline constexpr std::uint16_t kRumbleLengthMs = 0x7FFF;

[[nodiscard]] ff_effect make_rumble_effect(
    QuirkId quirk,
    os::FfEffectId id,
    proto::user_io::RumbleMotors motors
) noexcept;

class RumbleChannel {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static RumbleChannel probe(const os::Evdev& ev) noexcept;

    [[nodiscard]] bool capable() const noexcept { return capable_; }

    [[nodiscard]] Ex<void> play(os::Evdev& ev, QuirkId quirk, proto::user_io::RumbleMotors motors);

    [[nodiscard]] bool release(os::Evdev& ev) noexcept;

private:
    friend struct InputDeviceTestAccess;

    bool capable_ = false;
    std::optional<os::FfEffectId> id_{};
};

}
