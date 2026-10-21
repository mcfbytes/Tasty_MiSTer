// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neo_transform.h"

namespace mister::cores::neogeo {

void spr_convert(
    std::span<const std::uint16_t> buf_in,
    std::span<std::uint16_t> buf_out,
    std::uint32_t size
) noexcept {
    const auto src = buf_in.first(block32_extent(size));
    const auto dst = buf_out.first(size);
    for (std::uint32_t i = 0; i < size; ++i)
        dst[i] = src[spr_index(i)];
}

void spr_convert_skp(
    std::span<const std::uint16_t> buf_in,
    std::span<std::uint16_t> buf_out,
    std::uint32_t size
) noexcept {
    const auto src = buf_in.first(block32_extent(size));
    const auto dst = buf_out.first(skp_out_extent(size));
    for (std::uint32_t i = 0; i < size; ++i)
        dst[i << 1] = src[spr_index(i)];
}

void spr_convert_dbl(
    std::span<const std::uint16_t> buf_in,
    std::span<std::uint16_t> buf_out,
    std::uint32_t size
) noexcept {
    const auto src = buf_in.first(block64_extent(size));
    const auto dst = buf_out.first(size);
    for (std::uint32_t i = 0; i < size; ++i)
        dst[i] = src[spr_dbl_index(i)];
}

void fix_convert(
    std::span<const std::uint8_t> buf_in,
    std::span<std::uint8_t> buf_out,
    std::uint32_t size
) noexcept {
    const auto src = buf_in.first(block32_extent(size));
    const auto dst = buf_out.first(size);
    for (std::uint32_t i = 0; i < size; ++i)
        dst[i] = src[fix_index(i)];
}

void spr_bswap(std::span<std::uint32_t> buf, std::uint32_t size) noexcept {
    const auto words = buf.first(size);
    for (std::uint32_t i = 0; i < size; ++i) {
        const std::uint32_t w = words[i];
        words[i] = (w & 0xFF0000FFu) | ((w & 0xFF00u) << 8) | ((w & 0xFF0000u) >> 8);
    }
}

}
