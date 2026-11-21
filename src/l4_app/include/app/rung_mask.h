// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "app/rung_rows.h"
#include "infra/enum_set.h"
#include "infra/seat.h"

namespace mister::app {

class RungMask {
public:
    TASTY_SEAT_EXEMPT(component);
    using Bits = infra::EnumSet<RungSlot, std::uint16_t>;
    static_assert(static_cast<std::size_t>(RungSlot::kCount) <= 16, "one bit per slot");

    constexpr RungMask() noexcept = default;

    consteval RungMask(std::initializer_list<RungSlot> slots) : bits_(Bits::of(slots)) {}

    constexpr void insert(RungSlot s) noexcept { bits_.insert(s); }

    [[nodiscard]] constexpr bool contains(RungSlot s) const noexcept { return bits_.contains(s); }

    [[nodiscard]] constexpr bool any(RungMask m) const noexcept {
        return !bits_.intersection(m.bits_).is_empty();
    }

    [[nodiscard]] constexpr bool is_subset(RungMask m) const noexcept {
        return bits_.is_subset(m.bits_);
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_.is_empty(); }

    friend constexpr bool operator==(RungMask, RungMask) noexcept = default;

private:
    Bits bits_{};
};

static_assert(sizeof(RungMask) == 2);

}
