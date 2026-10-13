// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto::user_io {

struct RumbleMotors {
    std::uint16_t strong = 0;
    std::uint16_t weak = 0;
    friend constexpr bool operator==(const RumbleMotors&, const RumbleMotors&) = default;
};

[[nodiscard]] constexpr RumbleMotors rumble_motors(std::uint16_t word) noexcept {
    const unsigned w = word;
    return RumbleMotors{
        .strong = static_cast<std::uint16_t>((w & 0xFF00u) + (w >> 8u)),
        .weak = static_cast<std::uint16_t>((w << 8u) + (w & 0x00FFu)),
    };
}

static_assert(rumble_motors(0x0080) == RumbleMotors{.strong = 0x0000, .weak = 0x8080});
static_assert(rumble_motors(0xFFFF) == RumbleMotors{.strong = 0xFFFF, .weak = 0xFFFF});

}  // namespace mister::proto::user_io
