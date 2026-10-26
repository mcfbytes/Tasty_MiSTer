// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "app/rung_roster.h"
#include "infra/seat.h"

namespace mister::app {

class RungMask {
public:
    TASTY_SEAT_EXEMPT(component);
    static_assert(static_cast<std::size_t>(RungSlot::kCount) <= 16, "one bit per slot");

    constexpr RungMask() noexcept = default;

    consteval RungMask(std::initializer_list<RungSlot> slots) {
        for (const RungSlot s : slots) {
            if (s >= RungSlot::kCount)
                not_a_slot();
            bits_ = static_cast<std::uint16_t>(bits_ | bit_(s));
        }
    }

    constexpr void set(RungSlot s) noexcept { bits_ = static_cast<std::uint16_t>(bits_ | bit_(s)); }

    [[nodiscard]] constexpr bool has(RungSlot s) const noexcept { return (bits_ & bit_(s)) != 0; }

    [[nodiscard]] constexpr bool any(RungMask m) const noexcept { return (bits_ & m.bits_) != 0; }

    [[nodiscard]] constexpr std::uint16_t bits() const noexcept { return bits_; }

    friend constexpr bool operator==(RungMask, RungMask) noexcept = default;

private:
    static void not_a_slot() noexcept;

    [[nodiscard]] static constexpr std::uint16_t bit_(RungSlot s) noexcept {
        return static_cast<std::uint16_t>(1u << static_cast<unsigned>(s));
    }

    std::uint16_t bits_ = 0;
};

}
