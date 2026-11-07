// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/hd_rect.h"

namespace mister::app {

struct PlaneRow {
    std::int32_t v = 0;
    friend constexpr bool operator==(PlaneRow, PlaneRow) = default;
};

struct PxPlane {
    std::span<std::uint16_t> pixels{};

    std::size_t row_px = 0;

    [[nodiscard]] constexpr std::span<std::uint16_t> row(PlaneRow y) const noexcept {
        return pixels.subspan(static_cast<std::size_t>(y.v) * row_px, row_px);
    }

    [[nodiscard]] constexpr HdRect bounds() const noexcept {
        const std::size_t rows = row_px == 0 ? std::size_t{0} : pixels.size() / row_px;
        return HdRect{0, 0, static_cast<std::int32_t>(row_px), static_cast<std::int32_t>(rows)};
    }
};

}
