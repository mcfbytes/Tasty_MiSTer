// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::cores::atari8 {

struct SioChecksum {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SioChecksum plus(std::byte b) const noexcept {
        std::uint8_t sum = static_cast<std::uint8_t>(v + std::to_integer<std::uint8_t>(b));
        if (sum < v)
            ++sum;
        return SioChecksum{sum};
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(SioChecksum, SioChecksum) = default;
};

[[nodiscard]] constexpr std::byte sio_checksum(std::span<const std::byte> bytes) noexcept {
    SioChecksum sum{};
    for (const std::byte b : bytes)
        sum = sum.plus(b);
    return sum.byte();
}

}
