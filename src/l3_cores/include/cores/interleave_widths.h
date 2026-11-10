// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

namespace mister::cores::mra {

struct LaneCount {
    std::uint8_t v = 0;
    friend constexpr bool operator==(LaneCount, LaneCount) = default;
};

struct BitWidth {

    static constexpr std::uint8_t kMaxBits = 64;

    static constexpr std::uint8_t kByteBits = 8;

    std::uint8_t v = 0;

    [[nodiscard]] static constexpr BitWidth invalid() noexcept { return {}; }

    [[nodiscard]] static constexpr BitWidth of(std::uint32_t bits) noexcept {
        if (bits == 0 || bits > kMaxBits)
            return invalid();
        return BitWidth{static_cast<std::uint8_t>(bits)};
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return v != 0; }

    [[nodiscard]] constexpr bool is_byte_aligned() const noexcept { return v % kByteBits == 0; }

    [[nodiscard]] constexpr LaneCount byte_lanes() const noexcept {
        return LaneCount{static_cast<std::uint8_t>(v / kByteBits)};
    }

    friend constexpr bool operator==(BitWidth, BitWidth) = default;
    friend constexpr auto operator<=>(BitWidth, BitWidth) = default;
};

inline constexpr BitWidth kInterleaveByteBits{BitWidth::kByteBits};

struct InterleaveWidths {
    BitWidth input{};
    BitWidth output{};
    friend constexpr bool operator==(InterleaveWidths, InterleaveWidths) = default;
};

}
