// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/hd_layout.h"
#include "app/hd_look.h"
#include "app/hd_style.h"
#include "app/hd_theme.h"
#include "app/page_description.h"

namespace mister::app {

enum class HdTemplateId : std::uint8_t { Zaparoo, QuickBar, Guide, Pixel };

struct HdTemplateTraits {
    std::string_view family;
    std::string_view subtitle_sep;
    std::uint8_t panel_pct;

    std::uint8_t strip_panel_pct;

    std::uint8_t horizontal_tiers = 0;

    std::uint8_t backdrop_ramp_pct = 100;

    std::uint8_t pixel_em = 0;

    std::uint8_t band_pct = 0;

    HdGround ground = HdGround::Primary;
};

inline constexpr std::array<HdTemplateTraits, 4> kHdTemplateTraits{{
    {.family = "Noto Sans",
     .subtitle_sep = "\u00B7  ",
     .panel_pct = 36,
     .strip_panel_pct = 36,
     .horizontal_tiers = 0},
    {.family = "Noto Sans",
     .subtitle_sep = "\u00B7  ",
     .panel_pct = 36,
     .strip_panel_pct = 100,
     .horizontal_tiers = tier_bit(RowTier::Verb) | tier_bit(RowTier::Shelf),
     .band_pct = 20},
    {.family = "Noto Sans",
     .subtitle_sep = "\u00B7  ",
     .panel_pct = 36,
     .strip_panel_pct = 36,
     .horizontal_tiers = 0,
     .backdrop_ramp_pct = 35},
    {.family = "Pixel Operator 8",
     .subtitle_sep = " \u00B7 ",
     .panel_pct = 50,
     .strip_panel_pct = 50,
     .horizontal_tiers = 0,
     .pixel_em = 8,
     .ground = HdGround::WindowShade},
}};

inline constexpr std::size_t kHdTemplateCount = kHdTemplateTraits.size();

static_assert(
    static_cast<std::size_t>(HdTemplateId::Pixel) + 1 == kHdTemplateCount,
    "one traits row per template, in HdTemplateId order"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return !t.family.empty()
                && t.panel_pct > 0
                && t.panel_pct <= 100
                && t.strip_panel_pct > 0
                && t.strip_panel_pct <= 100;
        }
    ),
    "every traits row names a family and a panel width of 1-100%"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return t.backdrop_ramp_pct > 0 && t.backdrop_ramp_pct <= 100;
        }
    ),
    "a backdrop ramp is 1-100% of the theme's dim"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return t.pixel_em <= 16 && (t.pixel_em & (t.pixel_em - 1)) == 0;
        }
    ),
    "a pixel em is 0 or a power of two up to 16"
);
static_assert(
    std::ranges::none_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return (t.horizontal_tiers & tier_bit(RowTier::Place)) != 0;
        }
    ),
    "a place row may step on Left/Right; only verb and shelf tiers run across"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return t.strip_panel_pct == t.panel_pct || t.horizontal_tiers != 0;
        }
    ),
    "a strip width differs from the panel width only where some tier runs across"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return t.band_pct == 0
                || (t.band_pct <= 50 && (t.horizontal_tiers & tier_bit(RowTier::Verb)) != 0);
        }
    ),
    "a band draws the hub's verb rows across, at most half the output high"
);

[[nodiscard]] constexpr const HdTemplateTraits& hd_template_traits(HdTemplateId id) noexcept {
    return kHdTemplateTraits[static_cast<std::size_t>(id)];
}

[[nodiscard]] constexpr bool hd_runs_across(
    const HdTemplateTraits& traits,
    const PageDescription& page
) noexcept {
    const std::size_t n = std::min<std::size_t>(page.row_count, PageDescription::kMaxRows);
    for (std::size_t i = 0; i < n; ++i)
        if ((traits.horizontal_tiers & tier_bit(page.rows[i].tier)) != 0)
            return true;
    return false;
}

[[nodiscard]] constexpr bool hd_runs_band(
    const HdTemplateTraits& traits,
    const PageDescription& page
) noexcept {
    return traits.band_pct != 0 && page.hub.count != 0;
}

[[nodiscard]] constexpr std::uint8_t hd_panel_pct(
    const HdTemplateTraits& traits,
    const PageDescription& page
) noexcept {
    if (hd_runs_band(traits, page))
        return traits.panel_pct;
    return hd_runs_across(traits, page) ? traits.strip_panel_pct : traits.panel_pct;
}

[[nodiscard]] constexpr HdLayout hd_page_layout(
    const HdTemplateTraits& traits,
    const PageDescription& page,
    std::uint32_t w,
    std::uint32_t h,
    HdGameDisplay display
) noexcept {
    return layout_for(
        w,
        h,
        hd_panel_pct(traits, page),
        display,
        hd_runs_band(traits, page) ? traits.band_pct : std::uint8_t{0}
    );
}

struct HdLayoutDecl {
    std::string_view name;
    HdTemplateId tmpl = HdTemplateId::Zaparoo;

    std::uint8_t default_palette = 0;
};

inline constexpr std::array<HdLayoutDecl, 4> kHdLayouts{{
    {.name = "Standard", .tmpl = HdTemplateId::Zaparoo, .default_palette = 0},
    {.name = "Quick Bar", .tmpl = HdTemplateId::QuickBar, .default_palette = 0},
    {.name = "Guide", .tmpl = HdTemplateId::Guide, .default_palette = 5},
    {.name = "16-Bit", .tmpl = HdTemplateId::Pixel, .default_palette = 6},
}};

struct HdPaletteDecl {
    std::string_view name;
    HdStyle style;
};

inline constexpr std::array<HdPaletteDecl, 7> kHdPalettes{{
    {.name = "Zaparoo Dark", .style = kHdPresets[0]},
    {.name = "Classic Purple", .style = kHdPresets[1]},
    {.name = "Amber Phosphor", .style = kHdPresets[2]},
    {.name = "Game Boy", .style = kHdPresets[3]},
    {.name = "NES", .style = kHdPresets[4]},
    {.name = "Graphite", .style = kHdPresets[5]},
    {.name = "16-Bit Blue", .style = kHdPresets[6]},
}};

inline constexpr std::array<std::string_view, 2> kHdGameDisplays{"Overlay", "Squeeze"};
inline constexpr std::string_view kHdColorsAuto = "Auto";

inline constexpr std::uint8_t kHdLookUnset = 0xFF;

static_assert(
    kHdLayouts.size() < kHdLookUnset
        && kHdPalettes.size() + 1 < kHdLookUnset
        && kHdGameDisplays.size() <= 0xFFu,
    "every Layout and Colors byte is below the ini's unset byte"
);
static_assert(
    std::ranges::all_of(
        kHdLayouts,
        [](const HdLayoutDecl& l) {
            return l.default_palette < kHdPalettes.size() && !l.name.empty();
        }
    ),
    "every layout names itself and a palette of the table"
);

inline constexpr std::uint8_t kHdOpacityMin = 50;
inline constexpr std::uint8_t kHdOpacityMax = 100;
inline constexpr std::uint8_t kHdOpacityStep = 5;

static_assert(
    kHdOpacityMin % kHdOpacityStep == 0
        && kHdOpacityMax % kHdOpacityStep == 0
        && kHdOpacityDefault % kHdOpacityStep == 0
        && kHdOpacityMin <= kHdOpacityDefault
        && kHdOpacityDefault <= kHdOpacityMax,
    "the opacity bounds and default sit on the row's grid"
);

[[nodiscard]] constexpr HdLook hd_look_index(HdLook look) noexcept {
    if (look.layout >= kHdLayouts.size())
        look.layout = 0;
    if (look.colors > kHdPalettes.size())
        look.colors = 0;
    if (look.display >= kHdGameDisplays.size())
        look.display = 0;
    if (look.opacity < kHdOpacityMin || look.opacity > kHdOpacityMax)
        look.opacity = kHdOpacityDefault;
    return look;
}

[[nodiscard]] constexpr std::uint8_t hd_opacity_of(HdLook look) noexcept {
    return hd_look_index(look).opacity;
}

inline constexpr auto kHdOpacityNames = [] {
    std::array<std::array<char, 4>, kHdOpacityMax - kHdOpacityMin + 1> names{};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto pct = static_cast<unsigned>(kHdOpacityMin + i);
        auto& n = names[i];
        if (pct >= 100) {
            n = {
                static_cast<char>('0' + pct / 100),
                static_cast<char>('0' + pct / 10 % 10),
                static_cast<char>('0' + pct % 10),
                '%'
            };
        } else {
            n = {static_cast<char>('0' + pct / 10), static_cast<char>('0' + pct % 10), '%', '\0'};
        }
    }
    return names;
}();

[[nodiscard]] constexpr const HdLayoutDecl& hd_layout_decl(std::uint8_t layout) noexcept {
    return kHdLayouts[hd_look_index(HdLook{.layout = layout}).layout];
}

static_assert(
    kHdGameDisplays.size() == 2
        && kHdGameDisplays[0] == "Overlay"
        && kHdGameDisplays[1] == "Squeeze",
    "the Game display rows name HdGameDisplay's values in its order"
);

[[nodiscard]] constexpr HdGameDisplay hd_game_display_of(HdLook look) noexcept {
    return static_cast<HdGameDisplay>(hd_look_index(look).display);
}

[[nodiscard]] constexpr const HdPaletteDecl& hd_palette(HdLook look) noexcept {
    const HdLook at = hd_look_index(look);
    return at.colors == 0 ? kHdPalettes[hd_layout_decl(at.layout).default_palette]
                          : kHdPalettes[at.colors - 1u];
}

[[nodiscard]] inline HdTheme hd_theme_tokens(HdLook look) noexcept {
    HdTheme t = derive_theme(hd_palette(look).style);
    const HdTemplateTraits& traits = hd_template_traits(hd_layout_decl(look.layout).tmpl);
    t.backdrop_ramp_pct = traits.backdrop_ramp_pct;
    const bool overlay = hd_game_display_of(look) == HdGameDisplay::Overlay;
    t.glass = hd_glass_keys(t, traits.ground, overlay ? hd_opacity_of(look) : kHdOpacityMax);
    return t;
}

[[nodiscard]] constexpr std::uint8_t hd_horizontal_tiers(std::uint8_t layout) noexcept {
    return hd_template_traits(hd_layout_decl(layout).tmpl).horizontal_tiers;
}

[[nodiscard]] constexpr std::size_t hd_look_choices(HdLookField f) noexcept {
    switch (f) {
        case HdLookField::Layout:
            return kHdLayouts.size();
        case HdLookField::Colors:
            return kHdPalettes.size() + 1;
        case HdLookField::GameDisplay:
            return kHdGameDisplays.size();
        case HdLookField::Opacity:
            return (kHdOpacityMax - kHdOpacityMin) / kHdOpacityStep + 1u;
    }
    return 1;
}

[[nodiscard]] constexpr std::string_view hd_look_choice_name(HdLookField f, HdLook look) noexcept {
    const HdLook at = hd_look_index(look);
    switch (f) {
        case HdLookField::Layout:
            return kHdLayouts[at.layout].name;
        case HdLookField::Colors:
            return at.colors == 0 ? kHdColorsAuto : kHdPalettes[at.colors - 1u].name;
        case HdLookField::GameDisplay:
            return kHdGameDisplays[at.display];
        case HdLookField::Opacity: {
            const auto& n = kHdOpacityNames[at.opacity - kHdOpacityMin];
            return {n.data(), n[3] == '\0' ? 3u : 4u};
        }
    }
    return {};
}

[[nodiscard]] constexpr std::uint8_t hd_look_step(HdLookField f, HdLook look, int delta) noexcept {
    if (f == HdLookField::Opacity) {
        const unsigned at = hd_opacity_of(look);
        const unsigned up = (at / kHdOpacityStep + 1u) * kHdOpacityStep;
        const unsigned down = (at - 1u) / kHdOpacityStep * kHdOpacityStep;
        return static_cast<std::uint8_t>(
            delta < 0 ? std::max<unsigned>(down, kHdOpacityMin)
                      : std::min<unsigned>(up, kHdOpacityMax)
        );
    }
    const auto count = static_cast<unsigned>(hd_look_choices(f));
    const unsigned held = hd_look_get(look, f);
    const unsigned at = held < count ? held : 0u;
    return static_cast<std::uint8_t>((at + (delta < 0 ? count - 1u : 1u)) % count);
}

struct HdThemeDecl {
    std::uint8_t layout = 0;
    std::uint8_t colors = 0;
};

inline constexpr std::array<HdThemeDecl, 8> kHdThemes{{
    {.layout = 0, .colors = 0},
    {.layout = 0, .colors = 2},
    {.layout = 0, .colors = 3},
    {.layout = 0, .colors = 4},
    {.layout = 0, .colors = 5},
    {.layout = 1, .colors = 0},
    {.layout = 2, .colors = 0},
    {.layout = 3, .colors = 0},
}};

[[nodiscard]] constexpr const HdThemeDecl& hd_theme(std::uint8_t index) noexcept {
    return kHdThemes[index < kHdThemes.size() ? index : 0u];
}

}
