// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister {

inline constexpr std::uint64_t kHeaderSkipMask = 0x3FFu;

namespace detail {

inline constexpr std::uint32_t kCrcTableEntries = 256;
inline constexpr std::uint32_t kReflectedPolynomial = 0xEDB88320u;
inline constexpr unsigned kBitsPerByte = 8;
inline constexpr std::uint32_t kByteMask = 0xFFu;

inline constexpr std::array<std::uint32_t, kCrcTableEntries> kCrc32Table = [] {
    std::array<std::uint32_t, kCrcTableEntries> t{};
    for (std::uint32_t i = 0; i < kCrcTableEntries; ++i) {
        std::uint32_t c = i;
        for (unsigned k = 0; k < kBitsPerByte; ++k) {
            c = (c & 1u) != 0u ? (kReflectedPolynomial ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
    }
    return t;
}();

}

[[nodiscard]] constexpr std::uint32_t crc32_update(
    std::uint32_t crc,
    std::span<const std::uint8_t> data
) noexcept {
    std::uint32_t c = ~crc;
    for (const std::uint8_t b : data) {
        c = detail::kCrc32Table[(c ^ b) & detail::kByteMask] ^ (c >> 8);
    }
    return ~c;
}

[[nodiscard]] constexpr std::uint32_t crc32_of(std::span<const std::uint8_t> data) noexcept {
    return crc32_update(0, data);
}

[[nodiscard]] constexpr std::uint64_t crc32_header_skip(std::uint64_t total) noexcept {
    return total & kHeaderSkipMask;
}

}
