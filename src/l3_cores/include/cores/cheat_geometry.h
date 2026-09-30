// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::cores {

inline constexpr std::size_t kCheatTableBytes = 2048;

struct CheatGeometry {
    std::uint32_t unit = 16;
    std::uint16_t max_active = 128;
};

[[nodiscard]] constexpr CheatGeometry clamp_cheat_geometry(std::int32_t unit,
                                                           std::int32_t max_active) noexcept {
    CheatGeometry g;
    g.unit = static_cast<std::uint32_t>(unit > 0 ? unit : 16);

    std::int64_t max = max_active > 0 ? max_active : 128;
    if (max * static_cast<std::int64_t>(g.unit) > static_cast<std::int64_t>(kCheatTableBytes)) {
        max = static_cast<std::int64_t>(kCheatTableBytes / g.unit);
    }
    g.max_active = static_cast<std::uint16_t>(max);
    return g;
}

}  // namespace mister::cores
