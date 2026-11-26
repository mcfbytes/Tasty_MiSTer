// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>

#include "hal/types.h"

namespace mister::proto::user_io {

struct RumbleMotors {
    std::uint16_t strong = 0;
    std::uint16_t weak = 0;
    friend constexpr bool operator==(const RumbleMotors&, const RumbleMotors&) = default;
};

[[nodiscard]] constexpr RumbleMotors rumble_motors(hal::LinkWord w) noexcept {
    constexpr auto level = [](std::uint8_t b) {
        return static_cast<std::uint16_t>(
            (std::uint16_t{b} << std::numeric_limits<std::uint8_t>::digits) | b
        );
    };
    return RumbleMotors{.strong = level(w.high_byte()), .weak = level(w.low_byte())};
}

static_assert(
    rumble_motors(hal::LinkWord{0x0080}) == RumbleMotors{.strong = 0x0000, .weak = 0x8080}
);
static_assert(
    rumble_motors(hal::LinkWord{0xFFFF}) == RumbleMotors{.strong = 0xFFFF, .weak = 0xFFFF}
);

}
