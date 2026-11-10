// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstdint>

#include "app/hd_rect.h"
#include "app/output_pixels.h"
#include "app/percent.h"
#include "app/types.h"

namespace mister::app {

struct OutputSize {
    OutputPixels w = {};
    OutputPixels h = {};

    [[nodiscard]] constexpr HdRect bounds() const noexcept {
        return HdRect{0, 0, static_cast<std::int32_t>(w.v), static_cast<std::int32_t>(h.v)};
    }

    [[nodiscard]] constexpr PixelCount area() const noexcept { return w.area_with(h); }

    template <std::integral I>
    [[nodiscard]] constexpr OutputSize divided_by(I divisor) const noexcept {
        return OutputSize{w.divided_by(divisor), h.divided_by(divisor)};
    }

    [[nodiscard]] constexpr OutputSize or_default(OutputSize fallback) const noexcept {
        return empty() ? fallback : *this;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return w.empty() || h.empty(); }

    friend constexpr bool operator==(const OutputSize&, const OutputSize&) = default;
};

[[nodiscard]] constexpr PixelCount pixels_of(HdRect box) noexcept {
    return box.is_empty() ? PixelCount{}
                          : PixelCount::of(std::int64_t{box.w} * std::int64_t{box.h});
}

}
