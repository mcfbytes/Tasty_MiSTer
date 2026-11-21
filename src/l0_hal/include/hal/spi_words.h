// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "hal/types.h"

namespace mister::hal {

template <std::size_t N>
struct SpiWords {
    static constexpr std::size_t kWordBits = std::numeric_limits<std::uint16_t>::digits;
    static_assert(
        N >= 1 && N * kWordBits <= std::numeric_limits<std::uint64_t>::digits,
        "a SpiWords split fits one 64-bit value"
    );

    std::array<SpiWord, N> words{};

    [[nodiscard]] static constexpr SpiWords le(std::uint64_t v) noexcept {
        SpiWords out{};
        for (std::size_t i = 0; i < N; ++i)
            out.words[i] = SpiWord::from_half(v, i * kWordBits);
        return out;
    }

    [[nodiscard]] constexpr SpiWord operator[](std::size_t i) const noexcept { return words[i]; }

    friend constexpr bool operator==(SpiWords, SpiWords) = default;
};

static_assert(sizeof(SpiWords<3>) == 3 * sizeof(SpiWord));

}
