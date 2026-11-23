// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/affine.h"

namespace mister::cores::minimig {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct BusBytes {
    std::uint32_t v = 0;
    using affine_count = BusBytes;

    [[nodiscard]] static constexpr BusBytes of(std::span<const std::byte> data) noexcept {
        return BusBytes{static_cast<std::uint32_t>(data.size())};
    }

    friend constexpr bool operator==(BusBytes, BusBytes) = default;
    friend constexpr auto operator<=>(BusBytes, BusBytes) = default;
};

}
