// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

struct StatusWord {
    TASTY_SEAT_EXEMPT(const_shared);
    static constexpr unsigned kBits = 128;
    static constexpr unsigned kWords = 8;
    std::array<std::uint16_t, kWords> words{};

    [[nodiscard]] constexpr bool get_bit(StatusBit bit) const noexcept {
        if (bit.v >= kBits)
            return false;
        return ((static_cast<unsigned>(words[bit.v / 16u]) >> (bit.v % 16u)) & 1u) != 0u;
    }

    [[nodiscard]] static constexpr StatusWord from_bytes(
        std::span<const std::uint8_t, 16> b
    ) noexcept {
        StatusWord w{};
        for (unsigned i = 0; i < kWords; ++i)
            w.words[i] = static_cast<std::uint16_t>(b[2 * i] | (b[2 * i + 1] << 8));
        return w;
    }

    friend constexpr bool operator==(const StatusWord&, const StatusWord&) = default;
};

}
