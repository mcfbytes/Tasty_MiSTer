// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::proto {

class ConfSwitches {
public:
    TASTY_SEAT_EXEMPT(component);

    enum class Bit : std::uint16_t {
        VgaScaler = 1u << 2,
        Csync = 1u << 3,
        ForcedScandoubler = 1u << 4,
        Ypbpr = 1u << 5,
        Audio96k = 1u << 6,
        Dvi = 1u << 7,
        HdmiLimited1 = 1u << 8,
        VgaSog = 1u << 9,
        DirectVideo = 1u << 10,
        HdmiLimited2 = 1u << 11,
        VgaFb = 1u << 12,
        DirectVideo2 = 1u << 13,
    };

    constexpr ConfSwitches& set(Bit b, bool on = true) noexcept {
        if (on) bits_ = static_cast<std::uint16_t>(bits_ | static_cast<std::uint16_t>(b));
        return *this;
    }
    [[nodiscard]] constexpr bool has(Bit b) const noexcept {
        return (bits_ & static_cast<std::uint16_t>(b)) != 0;
    }
    [[nodiscard]] constexpr std::uint16_t word() const noexcept { return bits_; }

    friend constexpr bool operator==(ConfSwitches, ConfSwitches) = default;

private:
    std::uint16_t bits_ = 0;
};

static_assert(sizeof(ConfSwitches) == 2);

}  // namespace mister::proto
