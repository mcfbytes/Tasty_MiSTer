// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/hd_layout.h"
#include "app/hd_theme.h"
#include "app/page_description.h"
#include "app/tier_set.h"

namespace mister::app {

enum class HdTemplateId : std::uint8_t { Zaparoo, QuickBar, Guide, Pixel };

struct HdTemplateTraits {
    std::string_view family;
    std::string_view subtitle_sep;
    std::uint8_t panel_pct;

    std::uint8_t strip_panel_pct;

    TierSet horizontal_tiers = {};

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
     .horizontal_tiers = {}},
    {.family = "Noto Sans",
     .subtitle_sep = "\u00B7  ",
     .panel_pct = 36,
     .strip_panel_pct = 100,
     .horizontal_tiers = TierSet::of(RowTier::Verb, RowTier::Shelf),
     .band_pct = 20},
    {.family = "Noto Sans",
     .subtitle_sep = "\u00B7  ",
     .panel_pct = 36,
     .strip_panel_pct = 36,
     .horizontal_tiers = {},
     .backdrop_ramp_pct = 35},
    {.family = "Pixel Operator 8",
     .subtitle_sep = " \u00B7 ",
     .panel_pct = 50,
     .strip_panel_pct = 50,
     .horizontal_tiers = {},
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
            constexpr std::uint8_t kPanelPctMax = 100;
            return !t.family.empty()
                && t.panel_pct > 0
                && t.panel_pct <= kPanelPctMax
                && t.strip_panel_pct > 0
                && t.strip_panel_pct <= kPanelPctMax;
        }
    ),
    "every traits row names a family and a panel width of 1-100%"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            constexpr std::uint8_t kRampPctMax = 100;
            return t.backdrop_ramp_pct > 0 && t.backdrop_ramp_pct <= kRampPctMax;
        }
    ),
    "a backdrop ramp is 1-100% of the theme's dim"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            constexpr std::uint8_t kPixelEmMax = 16;
            return t.pixel_em <= kPixelEmMax && (t.pixel_em & (t.pixel_em - 1)) == 0;
        }
    ),
    "a pixel em is 0 or a power of two up to 16"
);
static_assert(
    std::ranges::none_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) { return t.horizontal_tiers.has(RowTier::Place); }
    ),
    "a place row may step on Left/Right; only verb and shelf tiers run across"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            return t.strip_panel_pct == t.panel_pct || t.horizontal_tiers.any();
        }
    ),
    "a strip width differs from the panel width only where some tier runs across"
);
static_assert(
    std::ranges::all_of(
        kHdTemplateTraits,
        [](const HdTemplateTraits& t) {
            constexpr std::uint8_t kBandPctMax = 50;
            return t.band_pct == 0
                || (t.band_pct <= kBandPctMax && t.horizontal_tiers.has(RowTier::Verb));
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
    const RowIndex end = RowIndex{} + page.listed();
    for (RowIndex i{}; i < end; i += RowCount::of(1))
        if (traits.horizontal_tiers.has(page.row(i).tier))
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
    OutputSize out,
    HdGameDisplay display
) noexcept {
    return layout_for(
        out,
        hd_panel_pct(traits, page),
        display,
        hd_runs_band(traits, page) ? traits.band_pct : std::uint8_t{0}
    );
}

}
