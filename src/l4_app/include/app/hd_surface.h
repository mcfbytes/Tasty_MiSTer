// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/fb_view.h"
#include "app/hd_rect.h"
#include "app/row_bytes.h"

namespace mister::app {

struct HdSurface {
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    RowBytes stride{};
    FbView::FbFormat format = FbView::FbFormat::Rgb565;
    std::array<std::uint32_t, 2> slot{};

    [[nodiscard]] constexpr HdRect bounds() const noexcept {
        return HdRect{0, 0, static_cast<std::int32_t>(w), static_cast<std::int32_t>(h)};
    }

    [[nodiscard]] constexpr bool stride_ok() const noexcept { return rgb565_row_holds(stride, w); }

    [[nodiscard]] constexpr std::size_t pixel_count() const noexcept {
        return frame_bytes() / kRgb565PixelBytes;
    }

    [[nodiscard]] constexpr std::size_t byte_offset(
        std::uint32_t x,
        std::uint32_t y
    ) const noexcept {
        return row_start(stride, y) + std::size_t{x} * kRgb565PixelBytes;
    }

    [[nodiscard]] constexpr std::size_t frame_bytes() const noexcept {
        return rows_span(stride, h);
    }

    [[nodiscard]] constexpr std::uint32_t row_len() const noexcept {
        return rgb565_row_pixels(stride);
    }
};

[[nodiscard]] constexpr std::optional<HdSurface> fit_surface(
    std::size_t region_len,
    std::uint32_t out_w,
    std::uint32_t out_h
) noexcept {

    constexpr std::uint64_t kFullPixels = 1920u * 1080u;
    constexpr std::uint32_t kBigOutputScale = 2;

    constexpr std::uint32_t kMinWidth = 320;
    constexpr std::uint32_t kMinHeight = 240;
    constexpr std::uint32_t kRowAlignMask = 16 - 1u;
    constexpr std::uint32_t kSlotAlignMask = kFramebufferSlotAlign - 1u;
    if (out_w == 0 || out_h == 0)
        return std::nullopt;
    std::uint32_t w = out_w;
    std::uint32_t h = out_h;
    if (std::uint64_t{out_w} * out_h > kFullPixels) {
        w = out_w / kBigOutputScale;
        h = out_h / kBigOutputScale;
    }
    if (w < kMinWidth || h < kMinHeight)
        return std::nullopt;
    const RowBytes stride = rgb565_row_aligned(w, kRowAlignMask);
    const std::uint32_t bytes = stride.span_u32(h);
    const std::uint32_t slot0 = kFramebufferPageBytes;
    const std::uint32_t slot1 = (slot0 + bytes + kSlotAlignMask) & ~kSlotAlignMask;
    if (std::uint64_t{slot1} + bytes > region_len)
        return std::nullopt;
    return HdSurface{
        .w = w,
        .h = h,
        .stride = stride,
        .format = FbView::FbFormat::Rgb565,
        .slot = {slot0, slot1}
    };
}

}
