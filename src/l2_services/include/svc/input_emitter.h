// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/joystick.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

inline constexpr unsigned kMaxPlayersEmit = 6;

struct AxisCal;

class InputEmitter {
    TASTY_SEAT_RESIDENT(RT);

public:
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

    Ex<unsigned> service_joysticks(hal::ISpiTransport& link, bool local_walk = true);
    Ex<bool> push_paddle(hal::ISpiTransport& link, PlayerIndex player, std::int32_t raw,
                         const AxisCal& cal);
    Ex<bool> push_spinner(hal::ISpiTransport& link, PlayerIndex player, std::int32_t delta);

    unsigned axis_range_zero() const noexcept { return axis_range_zero_; }

private:
    proto::JoystickPort joy_{};
    JoyMask joy_mask_[kMaxPlayersEmit]{};
    JoyMask autofire_[kMaxPlayersEmit]{};

    bool zero_armed_[kMaxPlayersEmit]{true, true, true, true, true, true};
    bool grabbed_ = false;
    bool osd_visible_ = false;
    unsigned axis_range_zero_ = 0;
};

}  // namespace mister::svc
