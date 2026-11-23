// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "infra/opt_ref.h"

namespace mister::cores::mac {

struct ListingIndex {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr ListingIndex of(std::byte wire) noexcept {
        return ListingIndex{std::to_integer<std::uint8_t>(wire)};
    }

    [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

    template <class T>
    [[nodiscard]] static constexpr ListingIndex end_of(std::span<const T> listing) noexcept {
        constexpr std::size_t kLast = std::numeric_limits<std::uint8_t>::max();
        return ListingIndex{static_cast<std::uint8_t>(std::min(listing.size(), kLast))};
    }

    [[nodiscard]] constexpr ListingIndex next() const noexcept {
        return ListingIndex{static_cast<std::uint8_t>(v + 1u)};
    }

    template <class T>
    [[nodiscard]] constexpr infra::OptRef<const T> in(std::span<const T> listing) const noexcept {
        if (v >= listing.size())
            return {};
        return infra::OptRef<const T>{listing[v]};
    }

    friend constexpr bool operator==(ListingIndex, ListingIndex) = default;
};

}
