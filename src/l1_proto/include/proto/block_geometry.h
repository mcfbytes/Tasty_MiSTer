// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>

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

struct SdStatParameter {
    std::uint16_t v = 0;

    enum : std::uint16_t { kReadOnly = 0x80 };

    static constexpr unsigned kSlotBits = std::numeric_limits<std::uint16_t>::digits;

    [[nodiscard]] static constexpr SdStatParameter of(SlotIndex slot, bool writable) noexcept {
        const std::uint16_t bit = slot.v < kSlotBits
            ? static_cast<std::uint16_t>(std::uint16_t{1u} << slot.v)
            : std::uint16_t{0u};
        return SdStatParameter{static_cast<std::uint16_t>(
            bit | (writable ? std::uint16_t{0u} : static_cast<std::uint16_t>(kReadOnly))
        )};
    }

    [[nodiscard]] constexpr bool read_only() const noexcept { return (v & kReadOnly) != 0u; }

    friend constexpr bool operator==(SdStatParameter, SdStatParameter) = default;
};

static_assert(sizeof(SdStatParameter) == 2);
static_assert((1u << kMaxAnnounceableSlot) < SdStatParameter::kReadOnly);
static_assert(SdStatParameter::of(SlotIndex{2}, true) == SdStatParameter{0x0004});
static_assert(SdStatParameter::of(SlotIndex{2}, false) == SdStatParameter{0x0084});
static_assert(SdStatParameter::of(SlotIndex{2}, false).read_only());

}
