// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "proto/types.h"

namespace mister::cores {

using ::mister::infra::affine::operator+;

struct CheatUnits {
    std::uint32_t v = 0;
    using affine_count = CheatUnits;

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(CheatUnits, CheatUnits) = default;
    friend constexpr auto operator<=>(CheatUnits, CheatUnits) = default;
};

inline constexpr std::uint32_t kCheatDefaultUnit = 16;

struct CheatUnit {
    std::uint32_t v = kCheatDefaultUnit;

    [[nodiscard]] constexpr CheatUnits count_of(proto::FileSize bytes) const noexcept {
        return CheatUnits{v == 0 ? 0u : static_cast<std::uint32_t>(bytes.v / v)};
    }

    [[nodiscard]] constexpr bool whole(proto::FileSize bytes) const noexcept {
        const proto::FileSize unit{v};
        return v != 0 && bytes.whole_units(unit) == bytes.units_covering(unit);
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    friend constexpr bool operator==(CheatUnit, CheatUnit) = default;
};

}
