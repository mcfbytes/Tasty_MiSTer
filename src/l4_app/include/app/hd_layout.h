// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

#include "app/hd_rect.h"

namespace mister::app {

enum class HdGameDisplay : std::uint8_t { Overlay, Squeeze };

struct HdLayout {
    HdRect surface{};
    HdRect panel{};

    HdRect band{};
    HdRect picture{};
    HdRect game{};
    friend bool operator==(const HdLayout&, const HdLayout&) = default;
};

[[nodiscard]] constexpr HdRect hd_menu_box(const HdLayout& L) noexcept {
    if (L.band.h <= 0)
        return L.panel;
    return {.x = 0, .y = 0, .w = L.surface.w, .h = L.panel.y + L.panel.h};
}

[[nodiscard]] constexpr HdRect hd_contain_4x3(HdRect r) noexcept {
    constexpr std::int64_t kPictureAspectW = 4, kPictureAspectH = 3, kCentre = 2;
    if (r.is_empty())
        return {};
    const std::int64_t rw = r.w;
    const std::int64_t rh = r.h;
    const bool wide = rw * kPictureAspectH >= rh * kPictureAspectW;
    const std::int64_t pw = wide ? rh * kPictureAspectW / kPictureAspectH : rw;
    const std::int64_t ph = wide ? rh : rw * kPictureAspectH / kPictureAspectW;
    if (pw <= 0 || ph <= 0)
        return {};
    return {
        .x = static_cast<std::int32_t>(r.x + (rw - pw) / kCentre),
        .y = static_cast<std::int32_t>(r.y + (rh - ph) / kCentre),
        .w = static_cast<std::int32_t>(pw),
        .h = static_cast<std::int32_t>(ph)
    };
}

[[nodiscard]] constexpr HdRect hd_beside_panel(const HdLayout& L) noexcept {
    const std::int32_t x = L.panel.x + L.panel.w;
    const std::int32_t y = L.band.h > 0 ? L.band.y + L.band.h : 0;
    if (x >= L.surface.w || y >= L.surface.h)
        return {};
    return {.x = x, .y = y, .w = L.surface.w - x, .h = L.surface.h - y};
}

[[nodiscard]] constexpr HdLayout layout_for(
    std::uint32_t w,
    std::uint32_t h,
    std::uint8_t panel_pct,
    HdGameDisplay display,
    std::uint8_t band_pct = 0
) noexcept {

    constexpr std::uint64_t kLayoutPercent = 100, kMinPanelPctOfHeight = 60, kMaxBandPct = 50;
    HdLayout L{};
    if (h == 0 || w == 0)
        return L;
    const auto ih = static_cast<std::int32_t>(h);
    const auto iw = static_cast<std::int32_t>(w);
    const auto pct = std::min<std::uint64_t>(panel_pct, kLayoutPercent);
    const auto panel_w = static_cast<std::int32_t>(std::min<std::uint64_t>(
        w,
        std::max<std::uint64_t>(
            std::uint64_t{w} * pct / kLayoutPercent,
            std::uint64_t{h} * kMinPanelPctOfHeight / kLayoutPercent
        )
    ));
    const auto band_h = static_cast<std::int32_t>(
        std::uint64_t{h} * std::min<std::uint64_t>(band_pct, kMaxBandPct) / kLayoutPercent
    );
    L.surface = {.x = 0, .y = 0, .w = iw, .h = ih};
    if (band_h > 0)
        L.band = {.x = 0, .y = 0, .w = iw, .h = band_h};
    L.panel = {.x = 0, .y = band_h, .w = panel_w, .h = ih - band_h};
    L.picture = hd_contain_4x3(display == HdGameDisplay::Squeeze ? hd_beside_panel(L) : L.surface);
    const std::int32_t x0 = std::max(L.picture.x, panel_w);
    const std::int32_t y0 = std::max(L.picture.y, band_h);
    const std::int32_t x1 = L.picture.x + L.picture.w;
    const std::int32_t y1 = L.picture.y + L.picture.h;
    if (x1 > x0 && y1 > y0)
        L.game = {.x = x0, .y = y0, .w = x1 - x0, .h = y1 - y0};
    return L;
}

}
