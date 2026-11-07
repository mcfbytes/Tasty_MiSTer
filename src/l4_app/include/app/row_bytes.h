// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/picture_row.h"
#include "infra/decimal_text.h"

namespace mister::app {

struct RowBytes {
    std::size_t v = 0;

    [[nodiscard]] constexpr bool lacks(RowBytes row) const noexcept { return v < row.v; }

    [[nodiscard]] constexpr std::span<const std::byte> head(
        std::span<const std::byte> line
    ) const noexcept {
        return line.first(v);
    }

    [[nodiscard]] constexpr std::uint32_t span_u32(std::uint32_t rows) const noexcept {
        return static_cast<std::uint32_t>(v * rows);
    }

    [[nodiscard]] constexpr bool reaches(
        std::size_t extent,
        RowBytes row,
        PictureRows height
    ) const noexcept {
        if (height == PictureRows{} || extent < row.v)
            return false;
        if (height == PictureRows{1})
            return true;
        return v > 0 && v >= row.v && height.fits_in((extent - row.v) / v + 1u);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }
};

inline constexpr std::size_t kRgb565PixelBytes = 2;

[[nodiscard]] constexpr RowBytes rgb565_row_aligned(
    std::uint32_t pixels,
    std::uint32_t align_mask
) noexcept {
    return RowBytes{
        ((pixels * static_cast<std::uint32_t>(kRgb565PixelBytes) + align_mask) & ~align_mask)
    };
}

[[nodiscard]] constexpr bool rgb565_row_holds(RowBytes row, std::uint32_t pixels) noexcept {
    return row.v >= std::size_t{pixels} * kRgb565PixelBytes && row.v % kRgb565PixelBytes == 0u;
}

[[nodiscard]] constexpr std::size_t row_start(RowBytes row, std::uint32_t y) noexcept {
    return std::size_t{y} * row.v;
}

[[nodiscard]] constexpr std::size_t row_start(RowBytes row, PictureRow y) noexcept {
    return std::size_t{y.v} * row.v;
}

[[nodiscard]] constexpr std::size_t rows_span(RowBytes row, std::uint32_t rows) noexcept {
    return row.v * static_cast<std::size_t>(rows);
}

[[nodiscard]] constexpr std::size_t rows_span(RowBytes row, PictureRows rows) noexcept {
    return row.v * std::size_t{rows.v};
}

[[nodiscard]] constexpr std::uint32_t rgb565_row_pixels(RowBytes row) noexcept {
    return static_cast<std::uint32_t>(row.v / kRgb565PixelBytes);
}

}
