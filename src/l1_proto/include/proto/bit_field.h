// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "proto/types.h"

namespace mister::proto {

enum class ExShift : std::uint8_t { None = 0, Plus32 = 1 };

struct BitField {
    StatusBit start{};
    std::uint8_t width = 0;

    constexpr bool valid() const noexcept { return width != 0; }
    constexpr StatusBit end() const noexcept {
        return StatusBit{static_cast<std::uint8_t>(start.v + width - 1)};
    }
    constexpr std::uint32_t mask() const noexcept {
        return width >= 32 ? 0xFFFF'FFFFu : ((1u << width) - 1u);
    }
    friend constexpr bool operator==(BitField, BitField) = default;
};
static_assert(sizeof(BitField) == 2);

[[nodiscard]] BitField decode_bit_ref(std::string_view spec, ExShift ex) noexcept;

}  // namespace mister::proto
