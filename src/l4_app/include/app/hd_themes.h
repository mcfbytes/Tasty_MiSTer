// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/hd_style.h"

namespace mister::app {

enum class HdTemplateId : std::uint8_t { Zaparoo };

struct HdTemplateTraits {
    std::string_view family;
    std::string_view subtitle_sep;
    std::uint8_t panel_pct;
};

inline constexpr std::array<HdTemplateTraits, 1> kHdTemplateTraits{{
    {.family = "Noto Sans", .subtitle_sep = "\u00B7  ", .panel_pct = 36},
}};

static_assert(
    static_cast<std::size_t>(HdTemplateId::Zaparoo) + 1 == kHdTemplateTraits.size(),
    "one traits row per template, in HdTemplateId order"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return !t.family.empty() && t.panel_pct > 0 && t.panel_pct <= 100;
        }
    ),
    "every traits row names a family and a panel width of 1-100%"
);

[[nodiscard]] constexpr const HdTemplateTraits& hd_template_traits(HdTemplateId id) noexcept {
    return kHdTemplateTraits[static_cast<std::size_t>(id)];
}

struct HdThemeDecl {
    std::string_view name;
    HdStyle style;
    HdTemplateId tmpl = HdTemplateId::Zaparoo;
};

inline constexpr std::array<HdThemeDecl, 5> kHdThemes{{
    {.name = "Zaparoo Dark", .style = kHdPresets[0]},
    {.name = "Classic Purple", .style = kHdPresets[1]},
    {.name = "Amber Phosphor", .style = kHdPresets[2]},
    {.name = "Game Boy", .style = kHdPresets[3]},
    {.name = "NES", .style = kHdPresets[4]},
}};

static_assert(kHdThemes.size() <= 0xFFu, "a theme index is one byte");

inline constexpr HdTemplateId kHdRasterTemplate = HdTemplateId::Zaparoo;
static_assert(
    std::ranges::all_of(
        kHdThemes,
        [](const HdThemeDecl& t) { return t.tmpl == kHdRasterTemplate; }
    ),
    "a theme on another template needs the renderer to re-make its raster on that edge"
);

[[nodiscard]] constexpr std::uint8_t hd_theme_index(std::uint8_t index) noexcept {
    return index < kHdThemes.size() ? index : std::uint8_t{0};
}

[[nodiscard]] constexpr const HdThemeDecl& hd_theme(std::uint8_t index) noexcept {
    return kHdThemes[hd_theme_index(index)];
}

}
