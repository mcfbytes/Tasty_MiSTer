// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister {

namespace detail {

inline constexpr std::array<std::uint32_t, 256> kCrc32Table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
    }
    return t;
}();

}  // namespace detail

[[nodiscard]] constexpr std::uint32_t crc32_update(std::uint32_t crc,
                                                   std::span<const std::uint8_t> data) noexcept {
    std::uint32_t c = ~crc;
    for (const std::uint8_t b : data) {
        c = detail::kCrc32Table[(c ^ b) & 0xFFu] ^ (c >> 8);
    }
    return ~c;
}

[[nodiscard]] constexpr std::uint32_t crc32_of(std::span<const std::uint8_t> data) noexcept {
    return crc32_update(0, data);
}

[[nodiscard]] constexpr std::uint64_t crc32_header_skip(std::uint64_t total) noexcept {
    return total & 0x3FFu;
}

}  // namespace mister
