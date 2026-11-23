// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "cores/minimig/lance_span.h"
#include "cores/minimig/lance_word.h"
#include "infra/affine.h"
#include "infra/byte_offset.h"

namespace mister::cores::minimig {

struct LanceAddr {
    std::uint32_t v = 0;
    using affine_count = LanceSpan;

    [[nodiscard]] static constexpr LanceAddr of_buffer(LanceWordPair w) noexcept {
        constexpr std::uint32_t kHighByte = std::numeric_limits<std::uint8_t>::max();
        return LanceAddr{w.low.v | ((std::uint32_t{w.high.v} & kHighByte) << kWordBits)};
    }

    [[nodiscard]] static constexpr LanceAddr of_words(LanceWordPair w) noexcept {
        return LanceAddr{w.low.v | (std::uint32_t{w.high.v} << kWordBits)};
    }

    [[nodiscard]] constexpr LanceAddr in_ram() const noexcept { return LanceAddr{v & kRamMask}; }

    [[nodiscard]] constexpr bool beyond_ram() const noexcept { return v > kRamMask; }

    [[nodiscard]] constexpr LanceAddr ring_base() const noexcept {
        constexpr std::uint32_t kRingBaseMask = 0x00FF'FFF8;
        return LanceAddr{v & kRingBaseMask};
    }

    [[nodiscard]] constexpr LanceAddr with_low(LanceWord w) const noexcept {
        constexpr std::uint32_t kHighHalf = 0xFFFF'0000;
        return LanceAddr{(v & kHighHalf) | w.v};
    }

    [[nodiscard]] constexpr LanceAddr with_high(LanceWord w) const noexcept {
        constexpr std::uint32_t kLowHalf = 0x0000'FFFF;
        return LanceAddr{(v & kLowHalf) | (std::uint32_t{w.v} << kWordBits)};
    }

    [[nodiscard]] constexpr infra::ByteOffset lane_offset() const noexcept {
        return infra::ByteOffset{(v ^ 1u) & kRamMask};
    }

    friend constexpr bool operator==(LanceAddr, LanceAddr) = default;
    friend constexpr auto operator<=>(LanceAddr, LanceAddr) = default;

private:
    static constexpr unsigned kWordBits = std::numeric_limits<std::uint16_t>::digits;

    static constexpr std::uint32_t kRamMask = 0x7FFF;
};

}
