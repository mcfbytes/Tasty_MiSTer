// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"
#include "infra/seat.h"

namespace mister::cores::n64 {

inline constexpr std::size_t kBootcodeBegin = 0x40;
inline constexpr std::size_t kBootcodeAleckEnd = 0xc00;
inline constexpr std::size_t kBootcodeEnd = 0x1000;

class BootcodeSum final {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] constexpr std::array<std::uint64_t, 2> sums(
        std::span<const std::uint8_t> chunk
    ) const noexcept {
        std::array<std::uint64_t, 2> out{};
        if (chunk.size() < kBootcodeEnd)
            return out;

        std::uint64_t sum = 0;
        std::size_t i = kBootcodeBegin;
        for (; i < kBootcodeAleckEnd; i += 4)
            sum += infra::load_le<std::uint32_t>(chunk.subspan(i).first<4>());
        out[1] = sum;
        for (; i < kBootcodeEnd; i += 4)
            sum += infra::load_le<std::uint32_t>(chunk.subspan(i).first<4>());
        out[0] = sum;
        return out;
    }
};

}
