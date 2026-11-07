// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "cores/mra_lane.h"
#include "cores/mra_lane_writes.h"
#include "cores/mra_unit_len.h"
#include "cores/types.h"

namespace mister::cores::mra {

inline constexpr std::uint32_t kNibbleBits = 4;
inline constexpr std::uint32_t kNibbleMask = 0xFu;

struct MapWalk {
    LaneIndex lane{};
    LaneWrites writes{};
    friend constexpr bool operator==(MapWalk, MapWalk) = default;
};

struct MapWord {
    std::uint32_t value = 0;
    std::uint32_t digits = 0;
    friend constexpr bool operator==(MapWord, MapWord) = default;

    [[nodiscard]] constexpr bool is_none() const noexcept { return value == 0; }

    [[nodiscard]] constexpr std::uint32_t value_or_one() const noexcept {
        return value == 0 ? 1u : value;
    }

    [[nodiscard]] constexpr bool plain() const noexcept { return value_or_one() == 1u; }

    [[nodiscard]] constexpr std::uint32_t nibble(std::uint32_t i) const noexcept {
        return nibble_of(value, i);
    }

    [[nodiscard]] constexpr std::optional<MapWalk> walk(UnitLen unit) const noexcept {
        std::uint32_t rest = value_or_one();
        std::optional<LaneIndex> first;
        LaneWrites writes;
        RomLength gaps{};
        for (const LaneIndex lane : unit.lanes()) {
            const std::uint32_t nib = rest & kNibbleMask;
            rest >>= kNibbleBits;
            if (nib == 0) {
                if (first)
                    gaps += RomLength{1};
                continue;
            }
            if (!first)
                first = lane;
            if (!writes.push(first->bytes() + RomLength{nib - 1u} + gaps))
                return std::nullopt;
        }
        if (!first)
            return std::nullopt;
        return MapWalk{*first, writes};
    }

private:
    [[nodiscard]] static constexpr std::uint32_t nibble_of(
        std::uint32_t word,
        std::uint32_t i
    ) noexcept {
        return (word >> (kNibbleBits * i)) & kNibbleMask;
    }
};

}
