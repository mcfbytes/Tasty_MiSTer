// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

#include "app/avi_frame_count.h"
#include "infra/decimal_text.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct AviFrameIndex {
    std::uint32_t v = 0;
    using affine_count = AviFrameCount;

    [[nodiscard]] constexpr infra::DecimalText digits() const noexcept {
        return infra::DecimalText{v};
    }

    friend constexpr bool operator==(AviFrameIndex, AviFrameIndex) = default;
    friend constexpr auto operator<=>(AviFrameIndex, AviFrameIndex) = default;
};

static_assert(sizeof(AviFrameIndex) == 4);

}
