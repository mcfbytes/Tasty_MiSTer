// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/enum_set.h"
#include "infra/seat.h"

namespace mister::app::hd {

enum class RowTier : std::uint8_t { Place, Verb, Shelf, kCount };

class TierSet {
    TASTY_SEAT_EXEMPT(component);

public:
    using Bits = infra::EnumSet<RowTier, std::uint8_t>;

    constexpr TierSet() noexcept = default;

    [[nodiscard]] constexpr bool contains(RowTier t) const noexcept { return bits_.contains(t); }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_.is_empty(); }

    constexpr void insert(RowTier t) noexcept { bits_.insert(t); }

    template <typename... Tiers>
        requires((std::is_same_v<Tiers, RowTier> && ...))
    [[nodiscard]] static constexpr TierSet of(Tiers... tiers) noexcept {
        TierSet out{};
        (out.insert(tiers), ...);
        return out;
    }

    friend constexpr bool operator==(TierSet, TierSet) = default;

private:
    Bits bits_{};
};

static_assert(sizeof(TierSet) == 1);

}
