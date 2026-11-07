// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "cores/cheat_unit.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

inline constexpr std::size_t kCheatTableBytes = 2048;

inline constexpr CheatUnits kCheatDefaultMaxActive{128};

struct CheatGeometry {
    CheatUnit unit{};
    CheatUnits max_active = kCheatDefaultMaxActive;
    friend constexpr bool operator==(CheatGeometry, CheatGeometry) = default;
};

[[nodiscard]] constexpr CheatGeometry clamp_cheat_geometry(CheatGeometry asked) noexcept {
    CheatGeometry g;
    if (!asked.unit.empty())
        g.unit = asked.unit;
    const CheatUnits want = asked.max_active == CheatUnits{} ? kCheatDefaultMaxActive
                                                             : asked.max_active;

    g.max_active = std::min(want, g.unit.count_of(proto::FileSize{kCheatTableBytes}));
    return g;
}

[[nodiscard]] constexpr CheatGeometry clamp_cheat_geometry(
    std::int32_t unit,
    std::int32_t max_active
) noexcept {
    return clamp_cheat_geometry(
        CheatGeometry{
            .unit = unit > 0 ? CheatUnit{static_cast<std::uint32_t>(unit)} : CheatUnit{},
            .max_active = max_active > 0 ? CheatUnits{static_cast<std::uint32_t>(max_active)}
                                         : kCheatDefaultMaxActive
        }
    );
}

}
