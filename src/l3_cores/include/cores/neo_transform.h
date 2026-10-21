// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

namespace mister::cores::neogeo {

[[nodiscard]] constexpr std::uint32_t spr_index(std::uint32_t i) noexcept {
    return (i & ~0x1Fu) | ((i >> 1) & 0xFu) | (((i & 1u) ^ 1u) << 4);
}

[[nodiscard]] constexpr std::uint32_t spr_dbl_index(std::uint32_t i) noexcept {
    return (i & ~0x3Fu) | ((i ^ 1u) & 1u) | ((i >> 1) & 0x1Eu) | (((i & 2u) ^ 2u) << 4);
}

[[nodiscard]] constexpr std::uint32_t fix_index(std::uint32_t i) noexcept {
    return (i & ~0x1Fu) | ((i >> 2) & 7u) | ((i & 1u) << 3) | (((i & 2u) << 3) ^ 0x10u);
}

[[nodiscard]] constexpr std::uint32_t block32_extent(std::uint32_t size) noexcept {
    return (size + 31u) & ~31u;
}

[[nodiscard]] constexpr std::uint32_t block64_extent(std::uint32_t size) noexcept {
    return (size + 63u) & ~63u;
}

[[nodiscard]] constexpr std::uint32_t skp_out_extent(std::uint32_t size) noexcept {
    return size == 0u ? 0u : (size << 1) - 1u;
}

void spr_convert(
    std::span<const std::uint16_t> buf_in,
    std::span<std::uint16_t> buf_out,
    std::uint32_t size
) noexcept;

void spr_convert_skp(
    std::span<const std::uint16_t> buf_in,
    std::span<std::uint16_t> buf_out,
    std::uint32_t size
) noexcept;

void spr_convert_dbl(
    std::span<const std::uint16_t> buf_in,
    std::span<std::uint16_t> buf_out,
    std::uint32_t size
) noexcept;

void fix_convert(
    std::span<const std::uint8_t> buf_in,
    std::span<std::uint8_t> buf_out,
    std::uint32_t size
) noexcept;

void spr_bswap(std::span<std::uint32_t> buf, std::uint32_t size) noexcept;

}
