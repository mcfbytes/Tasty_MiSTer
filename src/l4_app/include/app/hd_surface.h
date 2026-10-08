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

[[nodiscard]] constexpr std::optional<HdSurface> fit_surface(std::size_t region_len,
                                                             std::uint32_t out_w,
                                                             std::uint32_t out_h) noexcept {
    if (out_w == 0 || out_h == 0) return std::nullopt;
    std::uint32_t w = out_w;
    std::uint32_t h = out_h;
    if (static_cast<std::uint64_t>(out_w) * out_h > 1920u * 1080u) {
        w = out_w / 2u;
        h = out_h / 2u;
    }
    if (w < 320u || h < 240u) return std::nullopt;
    const std::uint32_t stride = (w * 2u + 15u) & ~15u;
    const std::uint32_t bytes = h * stride;
    const std::uint32_t slot0 = 4096u;
    const std::uint32_t slot1 = (slot0 + bytes + 4095u) & ~4095u;
    if (static_cast<std::uint64_t>(slot1) + bytes > region_len) return std::nullopt;
    return HdSurface{.w = w,
                     .h = h,
                     .stride = stride,
                     .format = FbView::FbFormat::Rgb565,
                     .slot = {slot0, slot1}};
}

}  // namespace mister::app
