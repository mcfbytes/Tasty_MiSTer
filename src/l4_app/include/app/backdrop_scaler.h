// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "app/hd_rect.h"
#include "app/hd_theme.h"
#include "app/px_plane.h"
#include "app/rgb_rows.h"
#include "infra/seat.h"

namespace mister::app {

struct BackdropScalerTarget {
    std::span<std::uint16_t> px{};
    HdRect rect{};
    std::uint16_t q8 = 0;
    std::uint16_t fill = 0;
};

class BackdropScaler {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void open(const HdTheme& theme) noexcept;

    [[nodiscard]] HdRect scale(
        RgbRows src,
        std::uint16_t src_w,
        PxPlane dst,
        HdRect game,
        HdRect picture,
        bool report
    ) noexcept;

    void scale_rows(
        RgbRows src,
        std::uint16_t src_w,
        PxPlane dst,
        HdRect game,
        HdRect picture,
        std::int32_t y0,
        std::int32_t y1
    ) noexcept;

    void scale_target_rows(
        RgbRows src,
        std::uint16_t src_w,
        HdRect picture,
        const BackdropScalerTarget& target,
        std::int32_t y0,
        std::int32_t y1
    ) noexcept;

private:
    void paint_band_(
        RgbRows src,
        std::uint16_t src_w,
        PxPlane dst,
        HdRect game,
        HdRect picture,
        std::int32_t y0,
        std::int32_t y1
    ) noexcept;
    void ensure_xmap_(std::uint16_t src_w, HdRect picture, HdRect shown, HdRect game) noexcept;

    std::uint16_t primary_ = 0;
    std::uint16_t q8_ = 102;
    std::uint8_t ramp_ = 100;
    std::uint16_t src_w_ = 0;
    std::int32_t xmap_picture_x_ = 0;
    std::int32_t xmap_picture_w_ = 0;
    std::int32_t xmap_shown_x_ = 0;
    std::vector<std::uint16_t> xmap_{};

    std::vector<std::uint16_t> qmap_{};
    std::int32_t qmap_shown_x_ = 0;
    std::int32_t qmap_game_x_ = 0;
    std::int32_t qmap_game_w_ = 0;
    std::vector<std::uint16_t> row_px_{};
    std::vector<std::uint16_t> target_cols_{};
    std::vector<std::uint16_t> target_px_{};
};

}
