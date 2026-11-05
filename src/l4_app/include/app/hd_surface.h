// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/fb_view.h"

namespace mister::app {

struct HdSurface {
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::uint32_t stride = 0;
    FbView::FbFormat format = FbView::FbFormat::Rgb565;
    std::array<std::uint32_t, 2> slot{};
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
    constexpr std::uint32_t kBytesPerPixel = 2;
    constexpr std::uint32_t kRowAlignMask = 16 - 1u;
    constexpr std::uint32_t kSlotAlignMask = 4096 - 1u;

    constexpr std::uint32_t kFirstSlot = 4096;
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
    const std::uint32_t stride = (w * kBytesPerPixel + kRowAlignMask) & ~kRowAlignMask;
    const std::uint32_t bytes = h * stride;
    const std::uint32_t slot0 = kFirstSlot;
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
