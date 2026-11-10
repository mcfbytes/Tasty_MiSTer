// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <span>

namespace mister::xthread {

struct ByteOffset {
    std::size_t v = 0;

    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return v; }

    [[nodiscard]] constexpr ByteOffset operator+(ByteOffset by) const noexcept {
        return ByteOffset{v + by.v};
    }

    template <std::size_t Width>
    [[nodiscard]] constexpr std::span<std::byte, Width> slice_of(
        std::span<std::byte> table
    ) const noexcept {
        return table.subspan(v).template first<Width>();
    }

    friend constexpr bool operator==(ByteOffset, ByteOffset) = default;
    friend constexpr auto operator<=>(ByteOffset, ByteOffset) = default;
};

}
