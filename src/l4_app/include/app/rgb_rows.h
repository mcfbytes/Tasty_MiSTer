// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/row_bytes.h"

namespace mister::app {

inline constexpr std::size_t kRgbPixelBytes = 3;

struct PictureRows {
    std::uint16_t v = 0;
    [[nodiscard]] constexpr bool operator==(const PictureRows&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PictureRows&) const noexcept = default;
};

struct RgbRows {
    std::span<const std::byte> bytes{};
    RowBytes stride{};
    PictureRows rows{};

    [[nodiscard]] constexpr bool covers(RowBytes row, PictureRows height) const noexcept {
        if (height == PictureRows{} || rows < height || bytes.size() < row.v)
            return false;
        if (height == PictureRows{1})
            return true;
        return stride.v > 0
            && stride.v >= row.v
            && (bytes.size() - row.v) / stride.v >= height.v - 1u;
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
