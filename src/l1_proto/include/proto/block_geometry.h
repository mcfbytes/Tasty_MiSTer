// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::proto {

inline constexpr unsigned kBlockSlots = 16;

inline constexpr std::uint32_t kBlockStagingBytes = 16384;

inline constexpr unsigned kMaxAnnounceableSlot = 6;

struct BlockGeometry {
    std::uint32_t block_size = 512;
    std::uint32_t window_blocks = 0;
};

}  // namespace mister::proto
