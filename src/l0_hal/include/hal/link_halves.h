// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>

#include "hal/types.h"

namespace mister::hal {

struct LinkHalves {
    LinkWord low;
    LinkWord high;

    [[nodiscard]] static constexpr mister::hal::LinkHalves of(std::uint32_t dword) noexcept {
        constexpr unsigned kHighHalfShift = std::numeric_limits<std::uint16_t>::digits;
        return LinkHalves{
            .low = LinkWord::from_half(dword, 0),
            .high = LinkWord::from_half(dword, kHighHalfShift)
        };
    }

    template <class Decode>
    [[nodiscard]] constexpr decltype(auto) joined(Decode&& decode) const noexcept {
        return decode(LinkWord::joined(low, high));
    }

    friend constexpr bool operator==(LinkHalves, LinkHalves) = default;
};

static_assert(sizeof(LinkHalves) == 4);

}
