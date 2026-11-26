// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/pin_sample.h"
#include "hal/types.h"
#include "infra/seat.h"

namespace mister::proto {

class ConfSwitches {
public:
    TASTY_SEAT_EXEMPT(component);

    enum class Bit : std::uint16_t {
        MenuButton = 1u << 0,
        UserButton = 1u << 1,
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
        if (on)
            bits_ = static_cast<std::uint16_t>(bits_ | static_cast<std::uint16_t>(b));
        return *this;
    }

    [[nodiscard]] constexpr ConfSwitches without(Bit b) const noexcept {
        ConfSwitches out = *this;
        out.bits_ = static_cast<std::uint16_t>(out.bits_ & ~static_cast<std::uint16_t>(b));
        return out;
    }

    [[nodiscard]] constexpr bool intersects(ConfSwitches bits) const noexcept {
        return (bits_ & bits.bits_) != 0;
    }

    [[nodiscard]] constexpr bool has(Bit b) const noexcept {
        return (bits_ & static_cast<std::uint16_t>(b)) != 0;
    }

    [[nodiscard]] constexpr ConfSwitches with_buttons(hal::ButtonMask buttons) const noexcept {
        ConfSwitches out = without(Bit::MenuButton).without(Bit::UserButton);
        if (buttons.intersects(hal::PinSample::kMenuButton))
            out.set(Bit::MenuButton);
        if (buttons.intersects(hal::PinSample::kUserButton))
            out.set(Bit::UserButton);
        return out;
    }

    [[nodiscard]] constexpr hal::ButtonMask buttons() const noexcept {
        hal::ButtonMask out{};
        if (has(Bit::MenuButton))
            out = hal::PinSample::kMenuButton;
        if (has(Bit::UserButton))
            out = out.with(hal::PinSample::kUserButton);
        return out;
    }

    [[nodiscard]] constexpr hal::LinkWord word() const noexcept { return hal::LinkWord{bits_}; }

    friend constexpr bool operator==(ConfSwitches, ConfSwitches) = default;

private:
    std::uint16_t bits_ = 0;
};

static_assert(sizeof(ConfSwitches) == 2);

}
