// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::proto {

inline constexpr unsigned kBlockSlots = 16;

inline constexpr std::uint32_t kBlockStagingBytes = 16384;

inline constexpr unsigned kMaxAnnounceableSlot = 6;

enum : std::uint32_t { kDefaultBlockBytes = 512 };

struct BlockGeometry {
    BlockSize block_size{kDefaultBlockBytes};
    BlockCount window_blocks{};
};

}
