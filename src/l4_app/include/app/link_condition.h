// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct LinkCondition {
    bool live = false;
    bool parked = false;
    bool starting = false;

    [[nodiscard]] constexpr std::uint8_t diag_byte() const noexcept {
        constexpr std::uint8_t kLiveBit = 1;
        constexpr std::uint8_t kParkedBit = 2;
        constexpr std::uint8_t kStartingBit = 4;
        std::uint8_t byte = 0;
        if (live)
            byte |= kLiveBit;
        if (parked)
            byte |= kParkedBit;
        if (starting)
            byte |= kStartingBit;
        return byte;
    }

    friend constexpr bool operator==(const LinkCondition&, const LinkCondition&) = default;
};

static_assert(LinkCondition{}.diag_byte() == 0, "no fact, no bit");
static_assert(
    LinkCondition{.live = true, .starting = true}.diag_byte() == 5,
    "live is bit 0, starting bit 2"
);

}
