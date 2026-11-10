// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::proto {

inline constexpr unsigned kBlockSlots = 16;

constexpr bool SlotIndex::in_block_slots() const noexcept {
    return v < kBlockSlots;
}

inline constexpr std::uint32_t kBlockStagingBytes = 16384;

inline constexpr unsigned kAnnounceableSlots = 7;
inline constexpr SlotIndex kLastAnnounceableSlot{kAnnounceableSlots - 1u};

enum : std::uint32_t { kDefaultBlockBytes = 512 };

struct BlockGeometry {
    BlockSize block_size{kDefaultBlockBytes};
    BlockCount window_blocks{};
};

static_assert(
    SdStatParameter::of(kLastAnnounceableSlot, false)
    == SdStatParameter{0x40u | SdStatParameter::kReadOnly}
);
static_assert(SdStatParameter::of(SlotIndex{2}, true) == SdStatParameter{0x0004});
static_assert(SdStatParameter::of(SlotIndex{2}, false) == SdStatParameter{0x0084});
static_assert(SdStatParameter::of(SlotIndex{2}, false).read_only());

}
