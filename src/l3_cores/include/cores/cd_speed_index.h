// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

namespace mister::cores {

struct CdSpeedIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(CdSpeedIndex, CdSpeedIndex) = default;
    friend constexpr auto operator<=>(CdSpeedIndex, CdSpeedIndex) = default;
};

}
