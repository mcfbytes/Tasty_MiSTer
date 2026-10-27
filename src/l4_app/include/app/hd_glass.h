// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "app/backdrop_scaler.h"
#include "app/hd_layout.h"
#include "app/hd_rect.h"
#include "app/hd_theme.h"
#include "infra/seat.h"

namespace mister::app {

struct HdGlassKernel {
    std::uint16_t ground_key = 0;
    std::uint16_t plate_key = 0;
    std::uint16_t ground_pre = 0;
    std::uint16_t plate_pre = 0;
};

void hd_glass_compose(
    const HdGlassKernel& k,
    std::span<const std::uint16_t> src,
    std::span<const std::uint16_t> under,
    std::span<std::uint16_t> out
) noexcept;

void hd_glass_compose_scalar(
    const HdGlassKernel& k,
    std::span<const std::uint16_t> src,
    std::span<const std::uint16_t> under,
    std::span<std::uint16_t> out
) noexcept;

class HdGlass {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::size_t kRects = 2;

    void relayout(
        const HdTheme& theme,
        const HdLayout& layout,
        std::int32_t w,
        std::int32_t h
    ) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] HdRect bounds() const noexcept;

    [[nodiscard]] std::array<BackdropScalerTarget, kRects> targets() noexcept;

    [[nodiscard]] HdRect span_at(std::int32_t y, std::int32_t x0, std::int32_t x1) const noexcept;

    [[nodiscard]] std::span<const std::uint16_t> compose(
        std::span<const std::uint16_t> src,
        std::int32_t x,
        std::int32_t y
    ) noexcept;

private:
    HdGlassKernel kernel_{};
    std::uint16_t q8_ = 0;
    std::uint16_t fill_ = 0;
    bool active_ = false;
    std::array<HdRect, kRects> rects_{};
    std::array<std::size_t, kRects> base_{};
    std::vector<std::uint16_t> under_{};
    std::vector<std::uint16_t> row_{};
};

}
