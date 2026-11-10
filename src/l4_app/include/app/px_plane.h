// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "app/hd_rect.h"
#include "app/types.h"

namespace mister::app {

struct PlaneRow {
    std::int32_t v = 0;
    friend constexpr bool operator==(PlaneRow, PlaneRow) = default;
};

struct PxPlane {
    std::span<std::uint16_t> pixels{};

    std::size_t row_px = 0;

    [[nodiscard]] constexpr std::span<std::uint16_t> row(PlaneRow y) const noexcept {
        const std::size_t at = static_cast<std::size_t>(y.v) * row_px;
        if (at >= pixels.size())
            return {};
        const std::size_t left = pixels.size() - at;
        return pixels.subspan(at, left < row_px ? left : row_px);
    }

    [[nodiscard]] constexpr HdRect bounds() const noexcept {
        const std::size_t rows = row_px == 0 ? std::size_t{0} : pixels.size() / row_px;
        return HdRect{0, 0, static_cast<std::int32_t>(row_px), static_cast<std::int32_t>(rows)};
    }

    [[nodiscard]] constexpr PixelIndex index_of(HdRect box) const noexcept {
        const std::size_t at = static_cast<std::size_t>(box.y) * row_px
            + static_cast<std::size_t>(box.x);
        constexpr std::size_t kMax = std::numeric_limits<std::uint32_t>::max();
        return PixelIndex::of(at < kMax ? at : kMax);
    }

    [[nodiscard]] constexpr std::optional<PxPlane> sub(HdRect box) const noexcept {
        if (box.x < 0 || box.y < 0)
            return std::nullopt;
        const auto tail = index_of(box).subspan(pixels);
        if (!tail)
            return std::nullopt;
        return PxPlane{.pixels = *tail, .row_px = row_px};
    }
};

}
