// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/affine.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct AviFrameCount {
    std::uint32_t v = 0;
    using affine_count = AviFrameCount;

    template <std::integral Unit>
    [[nodiscard]] constexpr auto scaled_by(Unit unit) const noexcept {
        return std::size_t{v} * unit;
    }

    friend constexpr bool operator==(AviFrameCount, AviFrameCount) = default;
    friend constexpr auto operator<=>(AviFrameCount, AviFrameCount) = default;
};

static_assert(sizeof(AviFrameCount) == 4);
static_assert(std::is_trivially_copyable_v<AviFrameCount>);

}
