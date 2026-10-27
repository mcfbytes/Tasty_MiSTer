// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backdrop_scaler.h"

#include <algorithm>
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
    const std::uint8_t r = theme.backdrop_ramp_pct;
    ramp_ = r == 0 ? std::uint8_t{1} : (r > 100 ? std::uint8_t{100} : r);
    src_w_ = 0;
    xmap_.clear();
    qmap_.clear();
    row_px_.clear();
}

void BackdropScaler::ensure_xmap_(
    std::uint16_t src_w,
    HdRect picture,
    HdRect shown,
    HdRect game
) noexcept {
    if (shown.w <= 0 || picture.w <= 0 || src_w == 0) {
        xmap_.clear();
        qmap_.clear();
        src_w_ = 0;
        return;
    }
    const auto n = static_cast<std::size_t>(shown.w);
    if (src_w_ != src_w
        || xmap_.size() != n
        || xmap_picture_x_ != picture.x
        || xmap_picture_w_ != picture.w
        || xmap_shown_x_ != shown.x) {
        src_w_ = src_w;
        xmap_picture_x_ = picture.x;
        xmap_picture_w_ = picture.w;
        xmap_shown_x_ = shown.x;
        xmap_.assign(n, 0);
        const auto off = static_cast<std::uint32_t>(shown.x - picture.x);
        for (std::int32_t x = 0; x < shown.w; ++x) {
            auto sx = (off + static_cast<std::uint32_t>(x))
                * src_w
                / static_cast<std::uint32_t>(picture.w);
            if (sx >= src_w)
                sx = static_cast<std::uint32_t>(src_w - 1);
            xmap_[static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(sx);
        }
    }
    if (qmap_.size() == n
        && qmap_shown_x_ == shown.x
        && qmap_game_x_ == game.x
        && qmap_game_w_ == game.w)
        return;
    qmap_shown_x_ = shown.x;
    qmap_game_x_ = game.x;
    qmap_game_w_ = game.w;
    qmap_.assign(n, q8_);
    if (ramp_ >= 100)
        return;
    const auto span = static_cast<std::uint64_t>(game.w > 1 ? game.w - 1 : 1);
    const std::uint64_t r = ramp_;
    for (std::int32_t x = 0; x < shown.w; ++x) {
        const std::int32_t at = shown.x + x - game.x;
        const auto dx = std::min<std::uint64_t>(static_cast<std::uint64_t>(at < 0 ? 0 : at), span);
        qmap_[static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(
            q8_ * (r * span + (100u - r) * dx) / (100u * span)
        );
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
    HdRect picture,
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
    paint_band_(
        rgb,
        src_w,
        src_h,
        src_line,
        frame,
        stride_px,
        game,
        picture,
        game.y,
        game.y + game.h
    );
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
    HdRect picture,
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
    paint_band_(rgb, src_w, src_h, src_line, frame, stride_px, game, picture, band0, band1);
}

void BackdropScaler::scale_target_rows(
    std::span<const std::byte> rgb,
    std::uint16_t src_w,
    std::uint16_t src_h,
    std::uint16_t src_line,
    HdRect picture,
    const BackdropScalerTarget& target,
    std::int32_t y0,
    std::int32_t y1
) noexcept {
    TASTY_SEAT_BODY(BackdropScaler);
    const HdRect r = target.rect;
    if (r.w <= 0 || r.h <= 0 || picture.w <= 0 || picture.h <= 0)
        return;
    const auto w = static_cast<std::size_t>(r.w);
    if (target.px.size() < w * static_cast<std::size_t>(r.h))
        return;
    const auto band0 = y0 > r.y ? y0 : r.y;
    const auto band1 = y1 < r.y + r.h ? y1 : r.y + r.h;
    if (band0 >= band1)
        return;
    const bool usable = src_w != 0
        && src_h != 0
        && src_line >= src_w * 3u
        && rgb.size() >= static_cast<std::size_t>(src_line) * src_h;
    if (!usable) {
        for (std::int32_t y = band0; y < band1; ++y) {
            const auto row = target.px.subspan(static_cast<std::size_t>(y - r.y) * w, w);
            std::fill(row.begin(), row.end(), target.fill);
        }
        return;
    }
    target_cols_.resize(w);
    target_px_.resize(w);
    for (std::size_t x = 0; x < w; ++x) {
        const auto at = static_cast<std::uint32_t>(r.x - picture.x) + static_cast<std::uint32_t>(x);
        auto sx = at * src_w / static_cast<std::uint32_t>(picture.w);
        if (sx >= src_w)
            sx = static_cast<std::uint32_t>(src_w - 1);
        target_cols_[x] = static_cast<std::uint16_t>(sx);
    }
    const std::uint32_t q = target.q8;
    const auto dim = [q](std::byte c) noexcept {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(c) * q) >> 8);
    };
    std::uint32_t packed_sy = 0xFFFFFFFFu;
    for (std::int32_t y = band0; y < band1; ++y) {
        auto sy = static_cast<std::uint32_t>(y - picture.y)
            * src_h
            / static_cast<std::uint32_t>(picture.h);
        if (sy >= src_h)
            sy = static_cast<std::uint32_t>(src_h - 1);
        if (sy != packed_sy) {
            packed_sy = sy;
            const auto src = rgb.subspan(static_cast<std::size_t>(sy) * src_line);
            for (std::size_t x = 0; x < w; ++x) {
                const auto p = src.subspan(static_cast<std::size_t>(target_cols_[x]) * 3u, 3);
                target_px_[x] = pack565(dim(p[0]), dim(p[1]), dim(p[2]));
            }
        }
        std::ranges::copy(
            target_px_,
            target.px.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y - r.y) * w)
        );
    }
}

void BackdropScaler::paint_band_(
    std::span<const std::byte> rgb,
    std::uint16_t src_w,
    std::uint16_t src_h,
    std::uint16_t src_line,
    std::span<std::uint16_t> frame,
    std::uint32_t stride_px,
    HdRect game,
    HdRect picture,
    std::int32_t y0,
    std::int32_t y1
) noexcept {
    const auto frame_h = static_cast<std::int32_t>(frame.size() / stride_px);
    const auto frame_w = static_cast<std::int32_t>(stride_px);
    const HdRect shown = intersect(clip_box(picture, frame_w, frame_h), game);
    const auto gx0 = game.x;
    const auto gx1 = game.x + game.w;
    const bool has_shown = shown.w > 0 && shown.h > 0;
    const auto bytes = static_cast<std::size_t>(src_line) * src_h;

    const bool draw_shown = has_shown && rgb.size() >= bytes;
    const auto cx0 = shown.x;
    const auto cx1 = shown.x + shown.w;
    const auto cy0 = shown.y;
    const auto cy1 = shown.y + shown.h;
    for (std::int32_t y = y0; y < y1; ++y) {
        auto* row = frame.data() + static_cast<std::size_t>(y) * stride_px;
        if (draw_shown && y >= cy0 && y < cy1) {
            for (std::int32_t x = gx0; x < cx0; ++x)
                row[x] = primary_;
            for (std::int32_t x = cx1; x < gx1; ++x)
                row[x] = primary_;
        } else {
            for (std::int32_t x = gx0; x < gx1; ++x)
                row[x] = primary_;
        }
    }
    if (!draw_shown)
        return;
    const auto dy0 = y0 > cy0 ? y0 : cy0;
    const auto dy1 = y1 < cy1 ? y1 : cy1;
    if (dy0 >= dy1)
        return;
    ensure_xmap_(src_w, picture, shown, game);
    if (row_px_.size() < static_cast<std::size_t>(shown.w))
        row_px_.resize(static_cast<std::size_t>(shown.w));
    const auto dim = [](std::uint8_t c, std::uint32_t q) noexcept {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(c) * q) >> 8);
    };
    std::uint32_t packed_sy = 0xFFFFFFFFu;
    for (std::int32_t y = dy0; y < dy1; ++y) {
        auto sy = static_cast<std::uint32_t>(y - picture.y)
            * src_h
            / static_cast<std::uint32_t>(picture.h);
        if (sy >= src_h)
            sy = static_cast<std::uint32_t>(src_h - 1);
        if (sy != packed_sy) {
            packed_sy = sy;
            const auto* src = rgb.data() + static_cast<std::size_t>(sy) * src_line;
            for (std::int32_t x = 0; x < shown.w; ++x) {
                const auto sx = xmap_[static_cast<std::size_t>(x)];
                const std::uint32_t q = qmap_[static_cast<std::size_t>(x)];
                const auto* p = src + static_cast<std::size_t>(sx) * 3u;
                row_px_[static_cast<std::size_t>(x)] = pack565(
                    dim(static_cast<std::uint8_t>(p[0]), q),
                    dim(static_cast<std::uint8_t>(p[1]), q),
                    dim(static_cast<std::uint8_t>(p[2]), q)
                );
            }
        }
        auto* dst = frame.data() + static_cast<std::size_t>(y) * stride_px + shown.x;
        std::memcpy(dst, row_px_.data(), static_cast<std::size_t>(shown.w) * sizeof(std::uint16_t));
    }
}

}
