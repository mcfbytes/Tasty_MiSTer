// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/picture_pixels.h"
#include "app/picture_row.h"
#include "app/row_bytes.h"

namespace mister::app {

inline constexpr std::size_t kRgbPixelBytes = 3;

[[nodiscard]] constexpr RowBytes rgb_row_of(PicturePixels pixels) noexcept {
    return pixels.row_bytes(PixelBytes::of(kRgbPixelBytes));
}

[[nodiscard]] constexpr RowBytes rgb_row_of(std::size_t pixels) noexcept {
    return RowBytes{pixels * kRgbPixelBytes};
}

struct RgbRows {
    std::span<const std::byte> bytes{};
    RowBytes stride{};
    PictureRows rows{};

    std::array<std::byte, 6> pad_{};

    [[nodiscard]] constexpr std::span<const std::byte> from_row(PictureRow row) const noexcept {
        return bytes.subspan(row_start(stride, row));
    }

    template <class F>
    constexpr void each_row(RowBytes width, F&& f) const {
        for_each_row(rows, [&](PictureRow y) { f(width.head(from_row(y))); });
    }

    [[nodiscard]] constexpr bool covers(RowBytes row, PictureRows height) const noexcept {
        return rows >= height && stride.reaches(bytes.size(), row, height);
    }
};

namespace rgb_rows_check {
inline constexpr std::array<std::byte, 64> kBuf{};

template <std::size_t N>
constexpr std::span<const std::byte> head() {
    return std::span{kBuf}.first<N>();
}

static_assert(
    !RgbRows{head<64>(), RowBytes{10}, PictureRows{4}}.covers(RowBytes{10}, PictureRows{0})
);
static_assert(RgbRows{head<8>(), RowBytes{4}, PictureRows{1}}.covers(RowBytes{8}, PictureRows{1}));
static_assert(
    !RgbRows{head<40>(), RowBytes{4}, PictureRows{4}}.covers(RowBytes{8}, PictureRows{4})
);
static_assert(
    !RgbRows{head<64>(), RowBytes{0}, PictureRows{4}}.covers(RowBytes{8}, PictureRows{4})
);
static_assert(
    !RgbRows{head<64>(), RowBytes{SIZE_MAX / 2}, PictureRows{4}}.covers(RowBytes{8}, PictureRows{4})
);
static_assert(
    RgbRows{head<38>(), RowBytes{10}, PictureRows{4}}.covers(RowBytes{8}, PictureRows{4})
);
static_assert(
    !RgbRows{head<37>(), RowBytes{10}, PictureRows{4}}.covers(RowBytes{8}, PictureRows{4})
);
static_assert(
    !RgbRows{head<38>(), RowBytes{10}, PictureRows{3}}.covers(RowBytes{8}, PictureRows{4})
);
}

}
