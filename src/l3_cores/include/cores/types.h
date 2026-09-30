// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

namespace mister::cores {

struct CoreTypeMask {
    std::uint8_t v = 0;
    friend constexpr bool operator==(CoreTypeMask, CoreTypeMask) = default;
};
inline constexpr CoreTypeMask kAnyDialect{0xFF};

[[nodiscard]] constexpr bool applies(CoreTypeMask row, CoreTypeMask dialect) noexcept {
    return (row.v & dialect.v) != 0u;
}

struct AcsiTarget {
    std::uint8_t v = 0;
    friend constexpr bool operator==(AcsiTarget, AcsiTarget) = default;
    friend constexpr auto operator<=>(AcsiTarget, AcsiTarget) = default;
};

struct ConfigSlot {
    std::uint8_t v = 0;
    friend constexpr bool operator==(ConfigSlot, ConfigSlot) = default;
};

struct WordId {
    std::uint8_t v = 0;
    friend constexpr bool operator==(WordId, WordId) = default;
};

}  // namespace mister::cores
