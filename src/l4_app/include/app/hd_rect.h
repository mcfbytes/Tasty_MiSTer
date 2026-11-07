// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct HdRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;

    [[nodiscard]] constexpr bool is_empty() const noexcept { return w <= 0 || h <= 0; }

    [[nodiscard]] constexpr HdRect intersect(const HdRect& o) const noexcept {
        if (is_empty() || o.is_empty())
            return {};
        const std::int32_t x0 = x > o.x ? x : o.x;
        const std::int32_t y0 = y > o.y ? y : o.y;
        const std::int32_t x1 = (x + w) < (o.x + o.w) ? (x + w) : (o.x + o.w);
        const std::int32_t y1 = (y + h) < (o.y + o.h) ? (y + h) : (o.y + o.h);
        if (x1 <= x0 || y1 <= y0)
            return {};
        return HdRect{x0, y0, x1 - x0, y1 - y0};
    }

    [[nodiscard]] constexpr HdRect union_with(const HdRect& o) const noexcept {
        if (o.is_empty())
            return *this;
        if (is_empty())
            return o;
        const std::int32_t x0 = x < o.x ? x : o.x;
        const std::int32_t y0 = y < o.y ? y : o.y;
        const std::int32_t x1 = x + w > o.x + o.w ? x + w : o.x + o.w;
        const std::int32_t y1 = y + h > o.y + o.h ? y + h : o.y + o.h;
        return HdRect{x0, y0, x1 - x0, y1 - y0};
    }

    friend constexpr bool operator==(const HdRect&, const HdRect&) = default;
};

}
