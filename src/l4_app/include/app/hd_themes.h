// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "app/hd_layout.h"
#include "app/hd_look.h"
#include "app/hd_style.h"
#include "app/hd_template_traits.h"
#include "app/hd_theme.h"
#include "app/tier_set.h"
#include "app/types.h"
#include "infra/decimal_text.h"
#include "infra/fixed_str.h"

namespace mister::app {

struct HdLayoutDecl {
    std::string_view name;
    HdTemplateId tmpl = HdTemplateId::Zaparoo;

    HdPaletteIndex default_palette{};
};

inline constexpr std::array<HdLayoutDecl, 4> kHdLayouts{{
    {.name = "Standard", .tmpl = HdTemplateId::Zaparoo, .default_palette = HdPaletteIndex{0}},
    {.name = "Quick Bar", .tmpl = HdTemplateId::QuickBar, .default_palette = HdPaletteIndex{0}},
    {.name = "Guide", .tmpl = HdTemplateId::Guide, .default_palette = HdPaletteIndex{5}},
    {.name = "16-Bit", .tmpl = HdTemplateId::Pixel, .default_palette = HdPaletteIndex{6}},
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
            return l.default_palette.within(kHdPalettes) && !l.name.empty();
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
    if (!look.layout.within(kHdLayouts))
        look.layout = HdLayoutIndex{};
    if (!look.colors.within(kHdPalettes))
        look.colors = HdColors{};
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
    constexpr std::string_view kPercentSuffix = "%";

    std::array<FixedStr<5, StrFit::Reject>, kHdOpacityMax - kHdOpacityMin + 1> names{};
    std::uint8_t pct = kHdOpacityMin;
    for (auto& name : names) {
        (void)name.assign(infra::DecimalText{pct}.view());
        (void)name.append(kPercentSuffix);
        ++pct;
    }
    return names;
}();

[[nodiscard]] constexpr const HdLayoutDecl& hd_layout_decl(HdLayoutIndex layout) noexcept {
    return hd_look_index(HdLook{.layout = layout}).layout.in(kHdLayouts);
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
    const std::optional<HdPaletteIndex> chosen = at.colors.palette();
    return (chosen ? *chosen : hd_layout_decl(at.layout).default_palette).in(kHdPalettes);
}

[[nodiscard]] inline HdTheme hd_theme_tokens(HdLook look) noexcept {
    HdTheme t = derive_theme(hd_palette(look).style);
    const HdTemplateTraits& traits = hd_template_traits(hd_layout_decl(look.layout).tmpl);
    t.backdrop_ramp_pct = traits.backdrop_ramp_pct;
    const bool overlay = hd_game_display_of(look) == HdGameDisplay::Overlay;
    t.glass = hd_glass_keys(t, traits.ground, overlay ? hd_opacity_of(look) : kHdOpacityMax);
    return t;
}

[[nodiscard]] constexpr TierSet hd_horizontal_tiers(HdLayoutIndex layout) noexcept {
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
            return at.layout.in(kHdLayouts).name;
        case HdLookField::Colors: {
            const std::optional<HdPaletteIndex> chosen = at.colors.palette();
            return chosen ? chosen->in(kHdPalettes).name : kHdColorsAuto;
        }
        case HdLookField::GameDisplay:
            return kHdGameDisplays[at.display];
        case HdLookField::Opacity:
            return kHdOpacityNames[at.opacity - kHdOpacityMin].view();
    }
    return {};
}

[[nodiscard]] constexpr std::uint8_t hd_look_step(
    HdLookField f,
    HdLook look,
    CycleDir dir
) noexcept {
    if (f == HdLookField::Opacity) {
        const unsigned at = hd_opacity_of(look);
        const unsigned up = (at / kHdOpacityStep + 1u) * kHdOpacityStep;
        const unsigned down = (at - 1u) / kHdOpacityStep * kHdOpacityStep;
        return static_cast<std::uint8_t>(
            dir == CycleDir::Back ? std::max<unsigned>(down, kHdOpacityMin)
                                  : std::min<unsigned>(up, kHdOpacityMax)
        );
    }
    const auto count = static_cast<unsigned>(hd_look_choices(f));
    const unsigned held = hd_look_get(look, f);
    const unsigned at = held < count ? held : 0u;
    return static_cast<std::uint8_t>((at + (dir == CycleDir::Back ? count - 1u : 1u)) % count);
}

}
