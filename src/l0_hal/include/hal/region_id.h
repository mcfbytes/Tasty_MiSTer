// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

enum class RegionId : std::uint8_t {

    MinimigShare,
    X86Share,
    X86Mem,
    MsuAudio,
    A2065Flat,
    SaturnCdBuf,

    VideoFb,
    ScalerOut,
};

inline constexpr std::size_t kRegionCount = static_cast<std::size_t>(RegionId::ScalerOut) + 1;

static_assert(kRegionCount <= 8, "RegionId grew: a core window belongs on its manifest");

}  // namespace mister::hal
