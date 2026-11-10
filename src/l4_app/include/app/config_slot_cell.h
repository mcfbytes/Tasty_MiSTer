// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "cores/config_codec.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct ConfigSlotMask {
    std::uint16_t v = 0;
    [[nodiscard]] constexpr bool has(cores::ConfigSlot slot) const noexcept;
    [[nodiscard]] constexpr ConfigSlotMask with(cores::ConfigSlot slot) const noexcept;
    friend constexpr bool operator==(ConfigSlotMask, ConfigSlotMask) = default;
};

[[nodiscard]] constexpr ConfigSlotMask slot_bit(cores::ConfigSlot slot) noexcept {
    if (slot.v >= std::numeric_limits<std::uint16_t>::digits)
        return ConfigSlotMask{};
    return ConfigSlotMask{static_cast<std::uint16_t>(std::uint16_t{1} << slot.v)};
}

constexpr bool ConfigSlotMask::has(cores::ConfigSlot slot) const noexcept {
    return (v & slot_bit(slot).v) != 0;
}

constexpr ConfigSlotMask ConfigSlotMask::with(cores::ConfigSlot slot) const noexcept {
    return ConfigSlotMask{static_cast<std::uint16_t>(v | slot_bit(slot).v)};
}

struct ConfigSlotTable {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint8_t count = 0;
    ConfigSlotMask exists{};
    FixedStr<cores::kConfigPreviewChars, StrFit::Clip> preview[cores::kMaxConfigSlots] = {};
};

static_assert(std::is_trivially_copyable_v<ConfigSlotTable>);
static_assert(cores::kMaxConfigSlots <= 16, "`exists` is a 16-bit mask");

using ConfigSlotCell = xthread::Telemetry<ConfigSlotTable, SeatTag::Main>;

}
