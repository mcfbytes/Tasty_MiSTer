// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_glass.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace mister::app {
namespace {

[[nodiscard]] HdRect overlap(HdRect a, HdRect b) noexcept {
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0)
        return {};
    const std::int32_t x0 = std::max(a.x, b.x);
    const std::int32_t y0 = std::max(a.y, b.y);
    const std::int32_t x1 = std::min(a.x + a.w, b.x + b.w);
    const std::int32_t y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return {};
    return HdRect{x0, y0, x1 - x0, y1 - y0};
}

[[nodiscard]] HdRect box_of(HdLayout::Rect r) noexcept {
    return HdRect{r.x, r.y, r.w, r.h};
}

[[nodiscard]] bool empty(HdRect r) noexcept {
    return r.w <= 0 || r.h <= 0;
}

}

void hd_glass_compose_scalar(
    const HdGlassKernel& k,
    std::span<const std::uint16_t> src,
    std::span<const std::uint16_t> under,
    std::span<std::uint16_t> out
) noexcept {
    const std::size_t n = std::min({src.size(), under.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint16_t x = src[i];
        if (x == k.ground_key)
            out[i] = static_cast<std::uint16_t>(k.ground_pre + under[i]);
        else if (x == k.plate_key)
            out[i] = static_cast<std::uint16_t>(k.plate_pre + under[i]);
        else
            out[i] = x;
    }
}

void hd_glass_compose(
    const HdGlassKernel& k,
    std::span<const std::uint16_t> src,
    std::span<const std::uint16_t> under,
    std::span<std::uint16_t> out
) noexcept {
    const std::size_t n = std::min({src.size(), under.size(), out.size()});
    std::size_t i = 0;
#if defined(__ARM_NEON)
    const uint16x8_t gk = vdupq_n_u16(k.ground_key);
    const uint16x8_t pk = vdupq_n_u16(k.plate_key);
    const uint16x8_t gp = vdupq_n_u16(k.ground_pre);
    const uint16x8_t pp = vdupq_n_u16(k.plate_pre);
    for (; i + 8 <= n; i += 8) {
        const uint16x8_t x = vld1q_u16(src.subspan(i, 8).data());
        const uint16x8_t u = vld1q_u16(under.subspan(i, 8).data());
        const uint16x8_t on_ground = vceqq_u16(x, gk);
        const uint16x8_t on_key = vorrq_u16(on_ground, vceqq_u16(x, pk));
        const uint16x8_t fill = vaddq_u16(vbslq_u16(on_ground, gp, pp), u);
        vst1q_u16(out.subspan(i, 8).data(), vbslq_u16(on_key, fill, x));
    }
#endif
    hd_glass_compose_scalar(
        k,
        src.subspan(i, n - i),
        under.subspan(i, n - i),
        out.subspan(i, n - i)
    );
}

void HdGlass::relayout(
    const HdTheme& theme,
    const HdLayout& layout,
    std::int32_t w,
    std::int32_t h
) noexcept {
    TASTY_SEAT_BODY(HdGlass);
    const HdThemeGlass& g = theme.glass;
    active_ = false;
    rects_ = {};
    base_ = {};
    if (!g.on() || empty(box_of(layout.game)))
        return;
    const HdRect surface{0, 0, w, h};
    const HdRect picture = overlap(box_of(layout.picture), surface);
    rects_ = {overlap(picture, box_of(layout.panel)), overlap(picture, box_of(layout.band))};
    std::size_t total = 0;
    std::int32_t widest = 0;
    for (std::size_t i = 0; i < kRects; ++i) {
        base_[i] = total;
        if (empty(rects_[i]))
            continue;
        total += static_cast<std::size_t>(rects_[i].w) * static_cast<std::size_t>(rects_[i].h);
        widest = std::max(widest, rects_[i].w);
    }
    if (total == 0)
        return;
    kernel_ = HdGlassKernel{
        .ground_key = hd_pack565(g.ground_key),
        .plate_key = hd_pack565(g.plate_key),
        .ground_pre = hd_scale_565(hd_pack565(g.ground), g.opacity),
        .plate_pre = hd_scale_565(hd_pack565(g.plate), g.opacity)
    };
    q8_ = hd_under_q8(g.opacity, g.under_dim);
    fill_ = hd_scale_565(hd_pack565(theme.primary), 100u - g.opacity);
    under_.assign(total, fill_);
    row_.assign(static_cast<std::size_t>(widest), 0);
    active_ = true;
}

HdRect HdGlass::bounds() const noexcept {
    HdRect out{};
    if (!active_)
        return out;
    for (const HdRect& r : rects_) {
        if (empty(r))
            continue;
        if (empty(out)) {
            out = r;
            continue;
        }
        const std::int32_t x1 = std::max(out.x + out.w, r.x + r.w);
        const std::int32_t y1 = std::max(out.y + out.h, r.y + r.h);
        out.x = std::min(out.x, r.x);
        out.y = std::min(out.y, r.y);
        out.w = x1 - out.x;
        out.h = y1 - out.y;
    }
    return out;
}

std::array<BackdropScalerTarget, HdGlass::kRects> HdGlass::targets() noexcept {
    std::array<BackdropScalerTarget, kRects> out{};
    if (!active_)
        return out;
    for (std::size_t i = 0; i < kRects; ++i) {
        if (empty(rects_[i]))
            continue;
        const auto n = static_cast<std::size_t>(rects_[i].w)
            * static_cast<std::size_t>(rects_[i].h);
        out[i] = BackdropScalerTarget{
            .px = std::span{under_}.subspan(base_[i], n),
            .rect = rects_[i],
            .q8 = q8_,
            .fill = fill_
        };
    }
    return out;
}

HdRect HdGlass::span_at(std::int32_t y, std::int32_t x0, std::int32_t x1) const noexcept {
    if (!active_)
        return {};
    for (const HdRect& r : rects_) {
        const HdRect s = overlap(r, HdRect{x0, y, x1 - x0, 1});
        if (!empty(s))
            return s;
    }
    return {};
}

std::span<const std::uint16_t> HdGlass::compose(
    std::span<const std::uint16_t> src,
    std::int32_t x,
    std::int32_t y
) noexcept {
    TASTY_SEAT_BODY(HdGlass);
    for (std::size_t i = 0; i < kRects; ++i) {
        const HdRect& r = rects_[i];
        if (!active_ || empty(r) || y < r.y || y >= r.y + r.h || x < r.x || x >= r.x + r.w)
            continue;
        const auto n = std::min<std::size_t>(src.size(), static_cast<std::size_t>(r.x + r.w - x));
        if (n > row_.size())
            continue;
        const std::size_t at = base_[i]
            + static_cast<std::size_t>(y - r.y) * static_cast<std::size_t>(r.w)
            + static_cast<std::size_t>(x - r.x);
        const auto out = std::span{row_}.first(n);
        hd_glass_compose(kernel_, src.first(n), std::span{under_}.subspan(at, n), out);
        return out;
    }
    return src;
}

}
