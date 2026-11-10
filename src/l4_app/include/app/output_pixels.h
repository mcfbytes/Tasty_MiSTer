// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstdint>

#include "app/percent.h"
#include "app/types.h"

namespace mister::app {

struct OutputPixels {
    std::uint32_t v = 0;
    using affine_count = OutputPixels;
    static constexpr std::uint32_t kHalfDivisor = 2;

    template <std::integral I>
    [[nodiscard]] static constexpr OutputPixels of(I n) noexcept {
        return OutputPixels{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr OutputPixels halved() const noexcept {
        return OutputPixels{v / kHalfDivisor};
    }

    template <std::integral I>
    [[nodiscard]] constexpr OutputPixels divided_by(I divisor) const noexcept {
        return OutputPixels{v / static_cast<std::uint32_t>(divisor)};
    }

    [[nodiscard]] constexpr OutputPixels percent(Percent p) const noexcept {
        return OutputPixels{p.share_of(v)};
    }

    [[nodiscard]] constexpr OutputPixels percent_rounded(Percent p) const noexcept {
        return OutputPixels{p.share_of_rounded(v)};
    }

    [[nodiscard]] static constexpr OutputPixels percent_of_height(
        OutputPixels height,
        Percent p
    ) noexcept {
        return height.percent(p);
    }

    [[nodiscard]] constexpr PixelCount area_with(OutputPixels rows) const noexcept {
        return PixelCount{static_cast<std::uint64_t>(v) * rows.v};
    }

    template <std::integral T>
    [[nodiscard]] constexpr T count() const noexcept {
        return static_cast<T>(v);
    }

    friend constexpr bool operator==(OutputPixels, OutputPixels) = default;
    friend constexpr auto operator<=>(OutputPixels, OutputPixels) = default;
};

[[nodiscard]] constexpr std::array<std::uint32_t, 2> fb_axis_words(
    OutputPixels width,
    OutputPixels height
) noexcept {
    return {width.v, height.v};
}

}
