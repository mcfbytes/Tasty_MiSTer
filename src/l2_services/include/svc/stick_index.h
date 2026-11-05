// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::svc {

struct StickIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(StickIndex, StickIndex) = default;
};

inline constexpr StickIndex kLeftStick{0};
inline constexpr StickIndex kRightStick{1};
inline constexpr std::array<StickIndex, 2> kSticks{kLeftStick, kRightStick};

}
