// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct FlipSeq {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr FlipSeq next() const noexcept { return FlipSeq{v + 1u}; }

    constexpr bool operator==(const FlipSeq&) const noexcept = default;
};

}
