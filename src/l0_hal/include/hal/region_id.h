// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

enum class RegionId : std::uint8_t {

    MinimigShare,
    X86Share,
    X86Mem,

    NicMailbox,
    VideoFb,
    ScalerOut,
};

template <class Table>
    requires requires(const Table& t) { t[std::size_t{}]; }
[[nodiscard]] constexpr const auto& row_of(const Table& table, RegionId id) noexcept {
    return table[static_cast<std::size_t>(id)];
}

inline constexpr std::size_t kRegionCount = static_cast<std::size_t>(RegionId::ScalerOut) + 1;

static_assert(kRegionCount <= 6, "RegionId grew: a core window belongs on its manifest");

}
