// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/mra_lane.h"
#include "cores/types.h"

namespace mister::cores::mra {

struct UnitLen {
    std::uint8_t v{1};
    friend constexpr bool operator==(UnitLen, UnitLen) = default;

    [[nodiscard]] constexpr std::span<const LaneIndex> lanes() const noexcept {
        return std::span<const LaneIndex>(kLanes).first(v);
    }

    [[nodiscard]] constexpr RomLength bytes() const noexcept { return RomLength{v}; }

    [[nodiscard]] constexpr RomLength span_of(RomLength chunk) const noexcept {
        return chunk * std::uint32_t{v};
    }

    [[nodiscard]] static constexpr UnitLen one() noexcept { return {}; }

    [[nodiscard]] static constexpr UnitLen of_map_digits(std::uint32_t digits) noexcept {
        return UnitLen{static_cast<std::uint8_t>(digits < kRomLanes ? digits : kRomLanes)};
    }

    [[nodiscard]] static constexpr std::optional<UnitLen> of_interleave(
        int input,
        int output
    ) noexcept {
        constexpr int kMaxOutputBits = 64;
        constexpr int kByteBitsMask = kInterleaveByteBits - 1;
        if (input != kInterleaveByteBits)
            return std::nullopt;
        if (output < kInterleaveByteBits
            || output > kMaxOutputBits
            || (output & kByteBitsMask) != 0)
            return std::nullopt;
        if (output < input)
            return std::nullopt;
        const int unit = (input != 0) ? output / input : 1;
        if (unit <= 0 || unit > static_cast<int>(kRomLanes))
            return std::nullopt;
        return UnitLen{static_cast<std::uint8_t>(unit)};
    }
};

}
