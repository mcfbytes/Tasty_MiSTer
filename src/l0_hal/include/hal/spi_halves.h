// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>

#include "hal/types.h"

namespace mister::hal {

struct SpiHalves {
    SpiWord low;
    SpiWord high;

    [[nodiscard]] static constexpr mister::hal::SpiHalves of(std::uint32_t dword) noexcept {
        constexpr unsigned kHighHalfShift = std::numeric_limits<std::uint16_t>::digits;
        return SpiHalves{
            .low = SpiWord::from_half(dword, 0),
            .high = SpiWord::from_half(dword, kHighHalfShift)
        };
    }

    template <class Decode>
    [[nodiscard]] constexpr decltype(auto) joined(Decode&& decode) const noexcept {
        return decode(SpiWord::joined(low, high));
    }

    friend constexpr bool operator==(SpiHalves, SpiHalves) = default;
};

static_assert(sizeof(SpiHalves) == 4);

}
