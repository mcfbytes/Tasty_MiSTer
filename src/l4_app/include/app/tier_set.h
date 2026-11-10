// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::app {

enum class RowTier : std::uint8_t { Place, Verb, Shelf };

[[nodiscard]] constexpr std::uint8_t tier_bit(RowTier t) noexcept {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(t));
}

struct TierSet {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool any() const noexcept { return v != 0; }

    [[nodiscard]] constexpr bool has(RowTier t) const noexcept { return (v & tier_bit(t)) != 0; }

    template <typename... Tiers>
        requires((std::is_same_v<Tiers, RowTier> && ...))
    [[nodiscard]] static constexpr TierSet of(Tiers... tiers) noexcept {
        return TierSet{static_cast<std::uint8_t>((tier_bit(tiers) | ... | 0u))};
    }

    friend bool operator==(const TierSet&, const TierSet&) = default;
};

}
