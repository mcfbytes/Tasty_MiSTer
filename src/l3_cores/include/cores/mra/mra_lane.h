// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/types.h"

namespace mister::cores::mra {

inline constexpr std::size_t kRomLanes = 8;

struct LaneIndex {
    std::uint8_t v{};
    friend constexpr bool operator==(LaneIndex, LaneIndex) = default;

    [[nodiscard]] static constexpr std::optional<LaneIndex> at(std::size_t i) noexcept {
        if (i >= kRomLanes)
            return std::nullopt;
        return LaneIndex{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr RomLength bytes() const noexcept { return RomLength{v}; }
};

inline constexpr std::array<LaneIndex, kRomLanes> kLanes = [] {
    std::array<LaneIndex, kRomLanes> lanes{};
    for (std::size_t i = 0; i < kRomLanes; ++i)
        lanes[i] = *LaneIndex::at(i);
    return lanes;
}();

inline constexpr std::span<const LaneIndex> kLanesAfterFirst = std::span<const LaneIndex>(kLanes)
                                                                   .subspan(1);

}
