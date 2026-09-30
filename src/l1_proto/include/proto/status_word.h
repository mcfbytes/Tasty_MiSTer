// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "proto/types.h"

namespace mister::proto {

struct StatusWord {
    static constexpr unsigned kBits = 128;
    static constexpr unsigned kWords = 8;
    std::array<std::uint16_t, kWords> words{};

    [[nodiscard]] constexpr bool get_bit(StatusBit bit) const noexcept {
        if (bit.v >= kBits) return false;
        return ((static_cast<unsigned>(words[bit.v / 16u]) >> (bit.v % 16u)) & 1u) != 0u;
    }
    friend constexpr bool operator==(const StatusWord&, const StatusWord&) = default;
};

}  // namespace mister::proto
