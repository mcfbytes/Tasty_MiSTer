// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::hal {

struct RegWord {
    std::uint32_t v = 0;

    enum : unsigned { kHalfBits = 16 };

    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return v; }

    [[nodiscard]] constexpr bool any_of(RegWord mask) const noexcept { return (v & mask.v) != 0u; }

    template <class Decode>
    [[nodiscard]] constexpr decltype(auto) decoded(Decode&& decode) const noexcept {
        return decode(v);
    }

    template <class Store>
    void emit(Store&& store) const noexcept {
        store(v);
    }

    [[nodiscard]] constexpr std::array<std::uint16_t, 2> halves() const noexcept {
        return {static_cast<std::uint16_t>(v), static_cast<std::uint16_t>(v >> kHalfBits)};
    }

    friend constexpr bool operator==(RegWord, RegWord) = default;
};

}
