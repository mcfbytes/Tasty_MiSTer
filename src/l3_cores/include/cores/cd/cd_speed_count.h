// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

#include "cores/cd/cd_speed_index.h"

namespace mister::cores::cd {

struct CdSpeedCount {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr CdSpeedIndex clamp(CdSpeedIndex i) const noexcept {
        return i < CdSpeedIndex{v} ? i : CdSpeedIndex{static_cast<std::uint8_t>(v - 1u)};
    }

    friend constexpr bool operator==(CdSpeedCount, CdSpeedCount) = default;
    friend constexpr auto operator<=>(CdSpeedCount, CdSpeedCount) = default;
};

inline constexpr CdSpeedCount kMaxSpeedCount{4};

}
