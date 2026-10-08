// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct SectorFloor {
    std::uint32_t bytes = 0;

    [[nodiscard]] constexpr bool is_sector(std::uint32_t phase_bytes) const noexcept {
        return bytes != 0 && phase_bytes >= bytes;
    }
};

}  // namespace mister::proto
