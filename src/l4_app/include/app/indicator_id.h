// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "infra/enum_set.h"
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
    using Bits = infra::EnumSet<IndicatorId, std::uint8_t>;

    constexpr IndicatorSet() noexcept = default;

    [[nodiscard]] constexpr bool contains(IndicatorId id) const noexcept {
        return bits_.contains(id);
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_.is_empty(); }

    [[nodiscard]] constexpr IndicatorSet with(IndicatorId id) const noexcept {
        IndicatorSet out = *this;
        out.bits_.insert(id);
        return out;
    }

    [[nodiscard]] constexpr IndicatorSet without(IndicatorId id) const noexcept {
        IndicatorSet out = *this;
        out.bits_.remove(id);
        return out;
    }

    friend constexpr bool operator==(const IndicatorSet&, const IndicatorSet&) = default;

private:
    Bits bits_{};
};

}
