// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "app/hd_look.h"
#include "app/hd_themes.h"
#include "app/types.h"
#include "svc/config_snapshot.h"

namespace mister::app {

struct HdStockTheme {
    HdLayoutIndex layout{};
    HdColors colors{};
};

inline constexpr std::array<HdStockTheme, 8> kHdStockThemes{{
    {.layout = HdLayoutIndex{0}, .colors = HdColors{0}},
    {.layout = HdLayoutIndex{0}, .colors = HdColors{2}},
    {.layout = HdLayoutIndex{0}, .colors = HdColors{3}},
    {.layout = HdLayoutIndex{0}, .colors = HdColors{4}},
    {.layout = HdLayoutIndex{0}, .colors = HdColors{5}},
    {.layout = HdLayoutIndex{1}, .colors = HdColors{0}},
    {.layout = HdLayoutIndex{2}, .colors = HdColors{0}},
    {.layout = HdLayoutIndex{3}, .colors = HdColors{0}},
}};

[[nodiscard]] constexpr const HdStockTheme& hd_stock_theme(StockThemeIndex index) noexcept {
    return index.pick(kHdStockThemes);
}

[[nodiscard]] inline HdLook hd_look_from_ini(const svc::ConfigSnapshot& c) noexcept {
    const HdStockTheme& old = hd_stock_theme(StockThemeIndex{c.hd_theme});
    return HdLook{
        .layout = c.hd_layout != kHdLookUnset ? HdLayoutIndex{c.hd_layout} : old.layout,
        .colors = c.hd_colors != kHdLookUnset ? HdColors{c.hd_colors} : old.colors,
        .display = c.hd_game_display,
        .opacity = c.hd_menu_opacity
    };
}

}
