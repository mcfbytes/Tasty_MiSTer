// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backdrop_scaler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace mister::app {
namespace {

[[nodiscard]] std::span<const std::byte, kRgbPixelBytes> pixel_at(
    std::span<const std::byte> row,
    std::size_t x
) noexcept {
    return std::span<const std::byte, kRgbPixelBytes>(
        row.subspan(x * kRgbPixelBytes, kRgbPixelBytes)
    );
}

[[nodiscard]] std::uint16_t pack_dimmed(
    std::span<const std::byte, kRgbPixelBytes> px,
    DimQ8 dim
) noexcept {
    const ColorChannel r{std::to_integer<std::uint8_t>(px[0])};
    const ColorChannel g{std::to_integer<std::uint8_t>(px[1])};
    const ColorChannel b{std::to_integer<std::uint8_t>(px[2])};
    return hd_pack565(rgb_from_channels(dim.apply(r), dim.apply(g), dim.apply(b)));
}

}

void BackdropScaler::open(const HdTheme& theme) noexcept {
    TASTY_SEAT_BODY(BackdropScaler);
    primary_ = hd_pack565(theme.primary);
    dim_ = DimQ8::of(theme.backdrop_dim);
    const std::uint8_t r = theme.backdrop_ramp_pct;
    ramp_ = r == 0 ? std::uint8_t{1} : (r > 100 ? std::uint8_t{100} : r);

    shown_dim_ = DimTable{.dim = dim_, .ramp = ramp_};
    row_px_.clear();
}

void BackdropScaler::ensure_cols_(
    ColumnMap& map,
    std::uint16_t src_w,
    const HdRect& picture,
    const HdRect& shown
) noexcept {
    if (map.src_w == src_w && map.picture == picture && map.shown == shown)
        return;
    map.src_w = src_w;
    map.picture = picture;
    map.shown = shown;
    map.cols.assign(static_cast<std::size_t>(shown.w), 0);
    const auto last = static_cast<std::uint32_t>(src_w - 1);
    const auto across = static_cast<std::uint32_t>(picture.w);
    const auto offset = static_cast<std::uint32_t>(shown.x - picture.x);
    for (std::int32_t x = 0; x < shown.w; ++x) {
        auto sx = (offset + static_cast<std::uint32_t>(x))
            * static_cast<std::uint32_t>(src_w)
            / across;
        if (sx > last)
            sx = last;
        map.cols[static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(sx);
    }
}

void BackdropScaler::ensure_dim_(
    DimTable& table,
    const HdRect& shown,
    const HdRect& game
) noexcept {
    constexpr std::uint64_t kPercent = 100;
    constexpr std::uint8_t kFlatPercent = 100;
    if (table.shown == shown && table.game == game)
        return;
    table.shown = shown;
    table.game = game;
    table.per_col.assign(static_cast<std::size_t>(shown.w), table.dim);
    if (table.ramp >= kFlatPercent)
        return;
    const auto span = static_cast<std::uint64_t>(game.w > 1 ? game.w - 1 : 1);
    const std::uint64_t r = table.ramp;
    for (std::int32_t x = 0; x < shown.w; ++x) {
        const std::int32_t at = shown.x + x - game.x;
        const auto dx = std::min<std::uint64_t>(static_cast<std::uint64_t>(at < 0 ? 0 : at), span);
        table.per_col[static_cast<std::size_t>(x)] = table.dim.eased(
            Ratio{
                .num = Ratio::Term::of(r * span + (kPercent - r) * dx),
                .den = Ratio::Term::of(kPercent * span)
            }
        );
    }
}

void BackdropScaler::scale_rows(
    RgbRows src,
    std::uint16_t src_w,
    PxPlane dst,
    HdRect game,
    HdRect picture,
    std::int32_t y0,
    std::int32_t y1
) noexcept {
    TASTY_SEAT_BODY(BackdropScaler);
    if (src_w == 0
        || src.rows == PictureRows{}
        || dst.row_px == 0
        || dst.pixels.empty()
        || src.stride.lacks(rgb_row_of(src_w)))
        return;
    game = game.intersect(dst.bounds());
    if (game.is_empty())
        return;
    const auto band0 = y0 > game.y ? y0 : game.y;
    const auto band1 = y1 < game.y + game.h ? y1 : game.y + game.h;
    if (band0 >= band1)
        return;
    paint_band_(src, src_w, dst, game, picture, band0, band1);
}

void BackdropScaler::scale_target_rows(
    RgbRows src,
    std::uint16_t src_w,
    HdRect picture,
    const BackdropScalerTarget& target,
    std::int32_t y0,
    std::int32_t y1
) noexcept {
    TASTY_SEAT_BODY(BackdropScaler);
    const HdRect r = target.rect;
    if (r.is_empty() || picture.is_empty())
        return;
    const auto w = static_cast<std::size_t>(r.w);
    if (target.px.size() < w * static_cast<std::size_t>(r.h))
        return;
    const auto band0 = y0 > r.y ? y0 : r.y;
    const auto band1 = y1 < r.y + r.h ? y1 : r.y + r.h;
    if (band0 >= band1)
        return;
    const bool usable = src_w != 0 && src.covers(rgb_row_of(src_w), src.rows);
    if (!usable) {
        for (std::int32_t y = band0; y < band1; ++y) {
            const auto row = target.px.subspan(static_cast<std::size_t>(y - r.y) * w, w);
            std::fill(row.begin(), row.end(), target.fill);
        }
        return;
    }
    ensure_cols_(target_cols_, src_w, picture, r);
    target_px_.resize(w);
    const DimQ8 dim = target.dim;
    std::optional<PictureRow> packed_sy;
    for (std::int32_t y = band0; y < band1; ++y) {
        const PictureRow sy = src.rows.sample_row(
            static_cast<std::uint32_t>(y - picture.y),
            PictureRows{static_cast<std::uint16_t>(picture.h)}
        );
        if (sy != packed_sy) {
            packed_sy = sy;
            const auto row = src.from_row(sy);
            for (std::size_t x = 0; x < w; ++x)
                target_px_[x] = pack_dimmed(pixel_at(row, target_cols_.cols[x]), dim);
        }
        std::ranges::copy(
            target_px_,
            target.px.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y - r.y) * w)
        );
    }
}

void BackdropScaler::paint_band_(
    RgbRows src,
    std::uint16_t src_w,
    PxPlane dst,
    HdRect game,
    HdRect picture,
    std::int32_t y0,
    std::int32_t y1
) noexcept {
    const HdRect shown = picture.intersect(dst.bounds()).intersect(game);
    const auto gx0 = game.x;
    const auto gx1 = game.x + game.w;
    const bool has_shown = !shown.is_empty();

    const bool draw_shown = has_shown && src.covers(rgb_row_of(src_w), src.rows);
    const auto cx0 = shown.x;
    const auto cx1 = shown.x + shown.w;
    const auto cy0 = shown.y;
    const auto cy1 = shown.y + shown.h;
    for (std::int32_t y = y0; y < y1; ++y) {
        auto* row = dst.row(PlaneRow{y}).data();
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
    ensure_cols_(shown_cols_, src_w, picture, shown);
    ensure_dim_(shown_dim_, shown, game);
    row_px_.resize(static_cast<std::size_t>(shown.w));
    std::optional<PictureRow> packed_sy;
    for (std::int32_t y = dy0; y < dy1; ++y) {
        const PictureRow sy = src.rows.sample_row(
            static_cast<std::uint32_t>(y - picture.y),
            PictureRows{static_cast<std::uint16_t>(picture.h)}
        );
        if (sy != packed_sy) {
            packed_sy = sy;
            const auto line = src.from_row(sy);
            for (std::int32_t x = 0; x < shown.w; ++x) {
                const auto i = static_cast<std::size_t>(x);
                row_px_[i] = pack_dimmed(
                    pixel_at(line, shown_cols_.cols[i]),
                    shown_dim_.per_col[i]
                );
            }
        }
        auto* out = dst.row(PlaneRow{y}).subspan(static_cast<std::size_t>(shown.x)).data();
        std::memcpy(out, row_px_.data(), static_cast<std::size_t>(shown.w) * sizeof(std::uint16_t));
    }
}

}
