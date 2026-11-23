// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/affine.h"
#include "infra/byte_count.h"

namespace mister::cores::minimig {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct LanceSpan {
    std::uint32_t v = 0;
    using affine_count = LanceSpan;

    [[nodiscard]] constexpr infra::ByteCount byte_count() const noexcept {
        return infra::ByteCount{v};
    }

    friend constexpr bool operator==(LanceSpan, LanceSpan) = default;
    friend constexpr auto operator<=>(LanceSpan, LanceSpan) = default;
};

}
