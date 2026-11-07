// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/user_io/rumble_poll.h"
#include "svc/rumble_pad_set.h"
#include "svc/types.h"

namespace mister::svc {

enum class RumbleApply : std::uint8_t { NoDevice, Applied, Failed };

class IRumbleOut {
public:
    [[nodiscard]] virtual RumblePadSet rumble_pads() const noexcept = 0;
    [[nodiscard]] virtual RumbleApply rumble(
        PlayerIndex player,
        proto::user_io::RumbleMotors motors
    ) noexcept = 0;

protected:
    IRumbleOut() = default;
    IRumbleOut(const IRumbleOut&) = default;
    IRumbleOut& operator=(const IRumbleOut&) = default;
    ~IRumbleOut() = default;
};

}
