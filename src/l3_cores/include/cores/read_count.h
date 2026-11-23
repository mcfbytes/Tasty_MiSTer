// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

namespace mister::cores {

struct ReadCount {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr ReadCount next() const noexcept { return ReadCount{v + 1u}; }

    friend constexpr bool operator==(ReadCount, ReadCount) = default;
    friend constexpr auto operator<=>(ReadCount, ReadCount) = default;
};

static_assert(ReadCount{}.next() > ReadCount{});

}
