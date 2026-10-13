// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

inline constexpr std::uint16_t kSdramCookieMeasured = 0x8000u;

[[nodiscard]] constexpr std::uint8_t sdram_size_code(std::uint16_t osd_mask) noexcept {
    switch (osd_mask & 7u) {
        case 7u:
            return 3u;
        case 3u:
            return 2u;
        case 1u:
            return 1u;
        default:
            return 0u;
    }
}

static_assert(sdram_size_code(7u) == 3u && sdram_size_code(3u) == 2u && sdram_size_code(1u) == 1u &&
              sdram_size_code(0u) == 0u && sdram_size_code(2u) == 0u && sdram_size_code(4u) == 0u &&
              sdram_size_code(5u) == 0u && sdram_size_code(6u) == 0u);
static_assert(kSdramCookieMeasured == 0x8000u && (kSdramCookieMeasured & 7u) == 0u);

}  // namespace mister::app
