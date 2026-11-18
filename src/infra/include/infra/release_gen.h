// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <climits>
#include <cstdint>
#include <type_traits>

namespace mister::xthread {

struct ReleaseGen {
    std::uint32_t v = 0;
    static constexpr std::uint32_t kHalf = (~0u >> CHAR_BIT) / 2 + 1;

    [[nodiscard]] static constexpr ReleaseGen none() noexcept { return ReleaseGen{}; }

    [[nodiscard]] static constexpr ReleaseGen max() noexcept { return ReleaseGen{~0u >> CHAR_BIT}; }

    [[nodiscard]] constexpr ReleaseGen next() const noexcept {
        return v >= max().v ? ReleaseGen{1} : ReleaseGen{v + 1};
    }

    [[nodiscard]] constexpr bool is_none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool covers(ReleaseGen asked) const noexcept {
        return ((v - asked.v) & max().v) < kHalf;
    }

    friend constexpr bool operator==(ReleaseGen, ReleaseGen) = default;
};

static_assert(
    sizeof(ReleaseGen) == 4 && std::is_trivially_copyable_v<ReleaseGen>,
    "the latch payload stays one lock-free word"
);

}
