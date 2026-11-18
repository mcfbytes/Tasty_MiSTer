// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bitset>
#include <cstdint>

#include "infra/message_sum.h"

namespace mister::app {

enum class IndicatorId : std::uint8_t {
    Saved,
    SaveFailed,
    kCount,
};

template <class T>
using PerIndicator = std::array<T, infra::ordinal(IndicatorId::kCount)>;

class IndicatorSet {
public:
    [[nodiscard]] constexpr bool has(IndicatorId id) const noexcept {
        return bits_.test(infra::ordinal(id));
    }

    [[nodiscard]] constexpr bool any() const noexcept { return bits_.any(); }

    [[nodiscard]] constexpr IndicatorSet with(IndicatorId id) const noexcept {
        IndicatorSet out = *this;
        out.bits_.set(infra::ordinal(id));
        return out;
    }

    [[nodiscard]] constexpr IndicatorSet without(IndicatorId id) const noexcept {
        IndicatorSet out = *this;
        out.bits_.reset(infra::ordinal(id));
        return out;
    }

    friend constexpr bool operator==(const IndicatorSet&, const IndicatorSet&) = default;

private:
    std::bitset<infra::ordinal(IndicatorId::kCount)> bits_{};
};

}
