// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <string_view>

#include "infra/affine.h"
#include "infra/byte_count.h"

namespace mister::infra {

struct ByteOffset {
    std::size_t v = 0;
    using affine_count = ByteCount;
    using affine_index = ByteOffset;

    [[nodiscard]] static constexpr ByteOffset of(ByteCount n) noexcept {
        return ByteOffset{n.size()};
    }

    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return v; }

    [[nodiscard]] constexpr char element_at(std::string_view text) const noexcept {
        return text[v];
    }

    [[nodiscard]] constexpr bool inside(std::string_view path) const noexcept {
        return v < path.size();
    }

    friend constexpr bool operator==(ByteOffset, ByteOffset) = default;
    friend constexpr auto operator<=>(ByteOffset, ByteOffset) = default;
};

}
