// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cores/mra_lane.h"
#include "cores/mra_unit_len.h"
#include "cores/types.h"

namespace mister::cores::mra {

class LaneWrites {
public:
    [[nodiscard]] constexpr bool push(RomLength offset) noexcept {
        if (count_ == kRomLanes)
            return false;
        offsets_[count_] = offset;
        ++count_;
        return true;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

    [[nodiscard]] constexpr RomLength per_unit() const noexcept { return RomLength{count_}; }

    [[nodiscard]] constexpr RomLength offset(std::size_t i) const noexcept { return offsets_[i]; }

    [[nodiscard]] constexpr RomLength highest() const noexcept {
        RomLength hi{};
        for (std::size_t i = 0; i < count_; ++i) {
            if (offsets_[i] > hi)
                hi = offsets_[i];
        }
        return hi;
    }

    [[nodiscard]] constexpr RomLength reach(RomLength chunk, UnitLen unitlen) const noexcept {
        const UnitCount units = chunk.units_covering(per_unit());
        return (units - UnitCount{1}).times(unitlen.bytes()) + highest() + RomLength{1};
    }

    friend constexpr bool operator==(LaneWrites, LaneWrites) = default;

private:
    std::array<RomLength, kRomLanes> offsets_{};
    std::uint32_t count_ = 0;
};

}
