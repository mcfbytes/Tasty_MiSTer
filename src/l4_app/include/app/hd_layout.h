// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace mister::app {

enum class HdGameDisplay : std::uint8_t { Overlay, Squeeze };

struct HdLayout {
    struct Rect {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t w = 0;
        std::int32_t h = 0;
        friend bool operator==(const Rect&, const Rect&) = default;
    };

    Rect surface{};
    Rect panel{};

    Rect band{};
    Rect picture{};
    Rect game{};
    friend bool operator==(const HdLayout&, const HdLayout&) = default;
};

[[nodiscard]] constexpr HdLayout::Rect hd_menu_box(const HdLayout& L) noexcept {
    if (L.band.h <= 0)
        return L.panel;
    return {.x = 0, .y = 0, .w = L.surface.w, .h = L.panel.y + L.panel.h};
}

[[nodiscard]] constexpr HdLayout::Rect hd_contain_4x3(HdLayout::Rect r) noexcept {
    if (r.w <= 0 || r.h <= 0)
        return {};
    const std::int64_t rw = r.w;
    const std::int64_t rh = r.h;
    const bool wide = rw * 3 >= rh * 4;
    const std::int64_t pw = wide ? rh * 4 / 3 : rw;
    const std::int64_t ph = wide ? rh : rw * 3 / 4;
    if (pw <= 0 || ph <= 0)
        return {};
    return {
        .x = static_cast<std::int32_t>(r.x + (rw - pw) / 2),
        .y = static_cast<std::int32_t>(r.y + (rh - ph) / 2),
        .w = static_cast<std::int32_t>(pw),
        .h = static_cast<std::int32_t>(ph)
    };
}

[[nodiscard]] constexpr HdLayout::Rect hd_beside_panel(const HdLayout& L) noexcept {
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
    HdLayout L{};
    if (h == 0 || w == 0)
        return L;
    const auto ih = static_cast<std::int32_t>(h);
    const auto iw = static_cast<std::int32_t>(w);
    const auto pct = std::min<std::uint64_t>(panel_pct, 100u);
    const auto panel_w = static_cast<std::int32_t>(std::min<std::uint64_t>(
        w,
        std::max<std::uint64_t>(
            static_cast<std::uint64_t>(w) * pct / 100u,
            static_cast<std::uint64_t>(h) * 60u / 100u
        )
    ));
    const auto band_h = static_cast<std::int32_t>(
        static_cast<std::uint64_t>(h) * std::min<std::uint64_t>(band_pct, 50u) / 100u
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
