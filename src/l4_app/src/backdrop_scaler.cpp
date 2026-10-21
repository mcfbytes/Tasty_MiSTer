// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backdrop_scaler.h"

#include <cstdint>
#include <cstring>

namespace mister::app {
namespace {

[[nodiscard]] std::uint16_t pack565(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(r >> 3) << 11)
        | (static_cast<std::uint16_t>(g >> 2) << 5)
        | (b >> 3)
    );
}

[[nodiscard]] std::uint16_t pack565_rgb(std::uint32_t rgb) noexcept {
    return pack565(
        static_cast<std::uint8_t>(rgb >> 16),
        static_cast<std::uint8_t>(rgb >> 8),
        static_cast<std::uint8_t>(rgb)
    );
}

[[nodiscard]] HdRect clip_box(HdRect r, std::int32_t w, std::int32_t h) noexcept {
    if (r.w <= 0 || r.h <= 0)
        return {};
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }
    if (r.w <= 0 || r.h <= 0 || r.x >= w || r.y >= h)
        return {};
    if (r.x + r.w > w)
        r.w = w - r.x;
    if (r.y + r.h > h)
        r.h = h - r.y;
    return r;
}

[[nodiscard]] HdRect intersect(HdRect a, HdRect b) noexcept {
    const std::int32_t x0 = a.x > b.x ? a.x : b.x;
    const std::int32_t y0 = a.y > b.y ? a.y : b.y;
    const std::int32_t x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const std::int32_t y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return {};
    return HdRect{x0, y0, x1 - x0, y1 - y0};
}

}

void BackdropScaler::open(const HdTheme& theme) noexcept {
    TASTY_SEAT_BODY(BackdropScaler);
    primary_ = pack565_rgb(theme.primary);
    const float d = theme.backdrop_dim < 0.f ? 0.f : theme.backdrop_dim;
    const auto q = static_cast<std::uint32_t>(d * 256.f + 0.5f);
    q8_ = q > 256u ? static_cast<std::uint16_t>(256) : static_cast<std::uint16_t>(q);
    src_w_ = 0;
    xmap_.clear();
    row_px_.clear();
}

void BackdropScaler::ensure_xmap_(std::uint16_t src_w, std::int32_t cover_w) noexcept {
    if (cover_w <= 0 || src_w == 0) {
        xmap_.clear();
        src_w_ = 0;
        return;
    }
    if (src_w_ == src_w && xmap_.size() == static_cast<std::size_t>(cover_w))
        return;
    src_w_ = src_w;
    xmap_.assign(static_cast<std::size_t>(cover_w), 0);
    for (std::int32_t x = 0; x < cover_w; ++x) {
        auto sx = static_cast<std::uint32_t>(x) * src_w / static_cast<std::uint32_t>(cover_w);
        if (sx >= src_w)
            sx = static_cast<std::uint32_t>(src_w - 1);
        xmap_[static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(sx);
    }
}

HdRect BackdropScaler::scale(
    std::span<const std::byte> rgb,
    std::uint16_t src_w,
    std::uint16_t src_h,
    std::uint16_t src_line,
    std::span<std::uint16_t> frame,
    std::uint32_t stride_px,
    HdRect game,
    HdRect cover,
    bool report
) noexcept {
    TASTY_SEAT_BODY(BackdropScaler);
    if (stride_px == 0 || frame.empty() || src_w == 0 || src_h == 0 || src_line < src_w * 3u)
        return {};
    const auto frame_h = static_cast<std::int32_t>(frame.size() / stride_px);
    const auto frame_w = static_cast<std::int32_t>(stride_px);
    game = clip_box(game, frame_w, frame_h);
    if (game.w <= 0 || game.h <= 0)
        return {};
    const HdRect cov = intersect(clip_box(cover, frame_w, frame_h), game);
    paint_band_(rgb, src_w, src_h, src_line, frame, stride_px, game, cov, game.y, game.y + game.h);
    return report ? game : HdRect{};
}

void BackdropScaler::scale_rows(
    std::span<const std::byte> rgb,
    std::uint16_t src_w,
    std::uint16_t src_h,
    std::uint16_t src_line,
    std::span<std::uint16_t> frame,
    std::uint32_t stride_px,
    HdRect game,
    HdRect cover,
    std::int32_t y0,
    std::int32_t y1
) noexcept {
    TASTY_SEAT_BODY(BackdropScaler);
    if (stride_px == 0 || frame.empty() || src_w == 0 || src_h == 0 || src_line < src_w * 3u)
        return;
    const auto frame_h = static_cast<std::int32_t>(frame.size() / stride_px);
    const auto frame_w = static_cast<std::int32_t>(stride_px);
    game = clip_box(game, frame_w, frame_h);
    if (game.w <= 0 || game.h <= 0)
        return;
    const auto band0 = y0 > game.y ? y0 : game.y;
    const auto band1 = y1 < game.y + game.h ? y1 : game.y + game.h;
    if (band0 >= band1)
        return;
    const HdRect cov = intersect(clip_box(cover, frame_w, frame_h), game);
    paint_band_(rgb, src_w, src_h, src_line, frame, stride_px, game, cov, band0, band1);
}

void BackdropScaler::paint_band_(
    std::span<const std::byte> rgb,
    std::uint16_t src_w,
    std::uint16_t src_h,
    std::uint16_t src_line,
    std::span<std::uint16_t> frame,
    std::uint32_t stride_px,
    HdRect game,
    HdRect cover,
    std::int32_t y0,
    std::int32_t y1
) noexcept {
    const auto gx0 = game.x;
    const auto gx1 = game.x + game.w;
    const bool has_cover = cover.w > 0 && cover.h > 0;
    const auto bytes = static_cast<std::size_t>(src_line) * src_h;

    const bool draw_cover = has_cover && rgb.size() >= bytes;
    const auto cx0 = cover.x;
    const auto cx1 = cover.x + cover.w;
    const auto cy0 = cover.y;
    const auto cy1 = cover.y + cover.h;
    for (std::int32_t y = y0; y < y1; ++y) {
        auto* row = frame.data() + static_cast<std::size_t>(y) * stride_px;
        if (draw_cover && y >= cy0 && y < cy1) {
            for (std::int32_t x = gx0; x < cx0; ++x)
                row[x] = primary_;
            for (std::int32_t x = cx1; x < gx1; ++x)
                row[x] = primary_;
        } else {
            for (std::int32_t x = gx0; x < gx1; ++x)
                row[x] = primary_;
        }
    }
    if (!draw_cover)
        return;
    const auto dy0 = y0 > cy0 ? y0 : cy0;
    const auto dy1 = y1 < cy1 ? y1 : cy1;
    if (dy0 >= dy1)
        return;
    ensure_xmap_(src_w, cover.w);
    if (row_px_.size() < static_cast<std::size_t>(cover.w))
        row_px_.resize(static_cast<std::size_t>(cover.w));
    const auto dim = [this](std::uint8_t c) noexcept {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(c) * q8_) >> 8);
    };
    std::uint32_t packed_sy = 0xFFFFFFFFu;
    for (std::int32_t y = dy0; y < dy1; ++y) {
        auto sy = static_cast<std::uint32_t>(y - cover.y)
            * src_h
            / static_cast<std::uint32_t>(cover.h);
        if (sy >= src_h)
            sy = static_cast<std::uint32_t>(src_h - 1);
        if (sy != packed_sy) {
            packed_sy = sy;
            const auto* src = rgb.data() + static_cast<std::size_t>(sy) * src_line;
            for (std::int32_t x = 0; x < cover.w; ++x) {
                const auto sx = xmap_[static_cast<std::size_t>(x)];
                const auto* p = src + static_cast<std::size_t>(sx) * 3u;
                row_px_[static_cast<std::size_t>(x)] = pack565(
                    dim(static_cast<std::uint8_t>(p[0])),
                    dim(static_cast<std::uint8_t>(p[1])),
                    dim(static_cast<std::uint8_t>(p[2]))
                );
            }
        }
        auto* dst = frame.data() + static_cast<std::size_t>(y) * stride_px + cover.x;
        std::memcpy(dst, row_px_.data(), static_cast<std::size_t>(cover.w) * sizeof(std::uint16_t));
    }
}

}
