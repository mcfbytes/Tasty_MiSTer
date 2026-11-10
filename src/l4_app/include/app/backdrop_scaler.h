// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "app/hd_rect.h"
#include "app/hd_theme.h"
#include "app/px_plane.h"
#include "app/rgb_rows.h"
#include "app/types.h"
#include "infra/seat.h"

namespace mister::app {

struct BackdropScalerTarget {
    std::span<std::uint16_t> px{};
    HdRect rect{};
    DimQ8 dim{};
    std::uint16_t fill = 0;
};

class BackdropScaler {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void open(const HdTheme& theme) noexcept;

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
    struct ColumnMap {
        std::uint16_t src_w = 0;
        HdRect picture{};
        HdRect shown{};
        std::vector<std::uint16_t> cols{};
    };

    struct DimTable {
        HdRect shown{};
        HdRect game{};
        DimQ8 dim{};
        std::uint8_t ramp = 0;
        std::vector<DimQ8> per_col{};
    };

    void ensure_cols_(
        ColumnMap& map,
        std::uint16_t src_w,
        const HdRect& picture,
        const HdRect& shown
    ) noexcept;

    void ensure_dim_(DimTable& table, const HdRect& shown, const HdRect& game) noexcept;
    void paint_band_(
        RgbRows src,
        std::uint16_t src_w,
        PxPlane dst,
        HdRect game,
        HdRect picture,
        std::int32_t y0,
        std::int32_t y1
    ) noexcept;

    std::uint16_t primary_ = 0;
    DimQ8 dim_ = DimQ8::of(kHdBackdropDim);
    std::uint8_t ramp_ = 100;
    ColumnMap shown_cols_{};
    DimTable shown_dim_{};
    ColumnMap target_cols_{};
    std::vector<std::uint16_t> row_px_{};
    std::vector<std::uint16_t> target_px_{};
};

}
