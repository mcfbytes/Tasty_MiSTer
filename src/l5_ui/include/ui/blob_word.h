// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace mister::ui {

struct BlobWord {
    std::uint8_t v = 0;

    template <class C>
    [[nodiscard]] constexpr bool indexes(const C& words) const noexcept {
        return v < std::size(words);
    }

    [[nodiscard]] constexpr BlobWord next() const noexcept {
        return BlobWord{static_cast<std::uint8_t>(v + 1u)};
    }

    friend constexpr bool operator==(BlobWord, BlobWord) = default;
    friend constexpr auto operator<=>(BlobWord, BlobWord) = default;
};

}
