// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/hd_rect.h"
#include "app/output_size.h"
#include "app/picture_pixels.h"
#include "app/picture_row.h"
#include "app/types.h"

namespace mister::app {

struct PictureArea;
struct PictureBox;
struct PictureFit;
struct PictureBlocks;

struct PictureSize {
    PicturePixels width{};
    PictureRows rows{};

    [[nodiscard]] constexpr PicturePixels pixels() const noexcept { return width; }

    [[nodiscard]] constexpr PictureRows lines() const noexcept { return rows; }

    [[nodiscard]] constexpr RowBytes stride_bytes(PixelBytes pixel_bytes) const noexcept {
        return width.row_bytes(pixel_bytes);
    }

    [[nodiscard]] constexpr RowBytes stride_bytes(
        PixelBytes pixel_bytes,
        RowAlign align
    ) const noexcept {
        return width.row_bytes(pixel_bytes, align);
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return width.empty() || rows.empty(); }

    [[nodiscard]] constexpr PictureSize halved() const noexcept {
        return PictureSize{width.halved(), rows.halved()};
    }

    [[nodiscard]] constexpr PictureSize halved_if(bool half) const noexcept {
        return half ? halved() : *this;
    }

    [[nodiscard]] constexpr PictureArea area() const noexcept;

    [[nodiscard]] constexpr bool fits_scaler_frame(PictureSize frame) const noexcept;

    [[nodiscard]] constexpr HdRect centre_in(PictureSize out) const noexcept;

    [[nodiscard]] constexpr PictureSize clamped_multiple_of(
        PictureSize wanted,
        PicturePixels min_side
    ) const noexcept;

    [[nodiscard]] constexpr PicturePixels blocks_across(PicturePixels block) const noexcept;

    [[nodiscard]] static constexpr PictureSize of(OutputSize out) noexcept;
    friend constexpr bool operator==(PictureSize, PictureSize) = default;
};

struct PictureArea {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr PictureArea of(PicturePixels width, PictureRows rows) noexcept {
        return PictureArea{
            static_cast<std::uint32_t>(width.v) * static_cast<std::uint32_t>(rows.v)
        };
    }

    friend constexpr bool operator==(PictureArea, PictureArea) = default;
    friend constexpr auto operator<=>(PictureArea, PictureArea) = default;
};

struct PictureBox {
    HdRect v{};

    [[nodiscard]] static constexpr PictureBox of(PictureSize picture, PictureSize out) noexcept {
        constexpr std::int32_t kDivisor = 2;
        const std::int32_t w = static_cast<std::int32_t>(picture.width.v);
        const std::int32_t h = static_cast<std::int32_t>(picture.rows.v);
        const std::int32_t out_w = static_cast<std::int32_t>(out.width.v);
        const std::int32_t out_h = static_cast<std::int32_t>(out.rows.v);
        return PictureBox{HdRect{(out_w - w) / kDivisor, (out_h - h) / kDivisor, w, h}};
    }

    [[nodiscard]] constexpr HdRect rect() const noexcept { return v; }
};

struct PictureFit {
    PictureSize v{};

    [[nodiscard]] constexpr PictureFit fitted_into(PictureSize bound) const noexcept {
        if (v.empty() || bound.empty())
            return PictureFit{};
        const std::uint64_t fit_w = static_cast<std::uint64_t>(bound.rows.v) * v.width.v;
        const std::uint64_t fit_h = static_cast<std::uint64_t>(bound.width.v) * v.rows.v;
        if (fit_w <= fit_h)
            return PictureFit{PictureSize{PicturePixels::of(fit_w / v.rows.v), bound.rows}};
        return PictureFit{PictureSize{bound.width, PictureRows::of(fit_h / v.width.v)}};
    }

    [[nodiscard]] constexpr PictureFit clamped_to(
        PictureSize wanted,
        PicturePixels min_side
    ) const noexcept {
        constexpr std::int32_t kFirstMultiple = 1;
        const std::int32_t out_w = static_cast<std::int32_t>(v.width.v);
        const std::int32_t out_h = static_cast<std::int32_t>(v.rows.v);
        if (out_w <= 0 || out_h <= 0)
            return PictureFit{};
        std::int32_t w = static_cast<std::int32_t>(wanted.width.v);
        std::int32_t h = static_cast<std::int32_t>(wanted.rows.v);
        const std::int32_t min = static_cast<std::int32_t>(min_side.v);
        if (w < min || w > out_w)
            w = out_w;
        if (h < min || h > out_h)
            h = out_h;
        std::int32_t d = kFirstMultiple;
        while (w * (d + kFirstMultiple) <= out_w && h * (d + kFirstMultiple) <= out_h)
            ++d;
        return PictureFit{PictureSize{PicturePixels::of(w * d), PictureRows::of(h * d)}};
    }

    [[nodiscard]] constexpr PictureSize size() const noexcept { return v; }
};

struct PictureBlocks {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr PictureBlocks of(
        PicturePixels width,
        PicturePixels block
    ) noexcept {
        constexpr std::uint16_t kStepOne = 1;
        const std::uint16_t step = block.v;
        return PictureBlocks{static_cast<std::uint16_t>((width.v + step - kStepOne) / step)};
    }

    [[nodiscard]] constexpr PicturePixels across() const noexcept { return PicturePixels{v}; }
};

constexpr PictureArea PictureSize::area() const noexcept {
    return PictureArea::of(width, rows);
}

constexpr bool PictureSize::fits_scaler_frame(PictureSize frame) const noexcept {
    return area() <= frame.area();
}

constexpr HdRect PictureSize::centre_in(PictureSize out) const noexcept {
    return PictureBox::of(*this, out).rect();
}

constexpr PictureSize PictureSize::clamped_multiple_of(
    PictureSize wanted,
    PicturePixels min_side
) const noexcept {
    return PictureFit{*this}.clamped_to(wanted, min_side).size();
}

constexpr PicturePixels PictureSize::blocks_across(PicturePixels block) const noexcept {
    return PictureBlocks::of(width, block).across();
}

constexpr PictureSize PictureSize::of(OutputSize out) noexcept {
    const HdRect box = out.bounds();
    return PictureSize{PicturePixels::of(box.w), PictureRows::of(box.h)};
}

}
