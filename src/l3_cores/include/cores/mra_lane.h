// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cores/types.h"

namespace mister::cores::mra {

inline constexpr std::size_t kRomLanes = 8;

inline constexpr int kInterleaveByteBits = 8;

struct LaneIndex {
    std::uint8_t v{};
    friend constexpr bool operator==(LaneIndex, LaneIndex) = default;

    [[nodiscard]] static constexpr LaneIndex at(std::size_t i) noexcept {
        return LaneIndex{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr RomLength bytes() const noexcept { return RomLength{v}; }
};

inline constexpr std::array<LaneIndex, kRomLanes> kLanes = [] {
    std::array<LaneIndex, kRomLanes> lanes{};
    for (std::size_t i = 0; i < kRomLanes; ++i)
        lanes[i] = LaneIndex::at(i);
    return lanes;
}();

}
