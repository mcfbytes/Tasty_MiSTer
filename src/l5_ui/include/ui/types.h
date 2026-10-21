// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

namespace mister::ui {

enum class KeyEdge : std::uint8_t { Make, Break };

struct ItemId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(ItemId, ItemId) = default;
};

inline constexpr ItemId kNoItem{0};

struct RowIndex {
    std::uint32_t v = 0;
    friend constexpr bool operator==(RowIndex, RowIndex) = default;
    friend constexpr auto operator<=>(RowIndex, RowIndex) = default;
};

struct RowKey {
    std::uint32_t v = 0;
    friend constexpr bool operator==(RowKey, RowKey) = default;
};

struct RowCount {
    std::uint32_t v = 0;
    friend constexpr bool operator==(RowCount, RowCount) = default;
    friend constexpr auto operator<=>(RowCount, RowCount) = default;
};

struct MenuMask {
    std::uint64_t v = 0;
    friend constexpr bool operator==(MenuMask, MenuMask) = default;
};

struct HdMask {
    std::uint32_t v = 0;
    friend constexpr bool operator==(HdMask, HdMask) = default;
};

}
