// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <array>
#include <cstdint>
#include <utility>

#include "app/output_size.h"

namespace mister::app {

[[nodiscard]] constexpr std::int32_t hd_sep_h(std::int32_t row_h) noexcept {
    constexpr std::int32_t kSeparatorDivisor = 2;
    return row_h / kSeparatorDivisor;
}

struct HdMetrics {
    std::int32_t tier = 720;
    std::int32_t font_hero = 0;
    std::int32_t font_title = 0;
    std::int32_t font_section = 0;
    std::int32_t font_body = 0;
    std::int32_t font_caption = 0;
    std::int32_t font_small = 0;
    std::int32_t inset_x = 0;
    std::int32_t inset_y = 0;
    std::int32_t pad_right = 0;
    std::int32_t header_gap = 0;
    std::int32_t list_top = 0;
    std::int32_t help_top = 0;
    std::int32_t help_pad = 0;
    std::int32_t gap = 0;
    std::int32_t row_h = 0;
    std::int32_t row_pad = 0;
    std::int32_t card_pad = 0;
    std::int32_t radius_md = 0;
    std::int32_t radius_sm = 0;
    std::int32_t stroke = 1;
    std::int32_t focus_stroke = 1;
    std::int32_t ring_w = 1;
    std::int32_t press_edge = 1;
    std::int32_t glyph = 0;
    std::int32_t icon = 0;
    std::int32_t button_h = 0;
    std::int32_t track_h = 0;
};

[[nodiscard]] constexpr std::int32_t hd_tier(OutputPixels h) noexcept {
    constexpr std::array<std::pair<std::uint32_t, std::int32_t>, 4> kCeilings{
        {{400, 240}, {520, 480}, {660, 540}, {900, 720}}
    };
    constexpr std::int32_t kTop = 1080;
    for (const auto& [below, tier] : kCeilings)
        if (h < OutputPixels::of(below))
            return tier;
    return kTop;
}

[[nodiscard]] constexpr HdMetrics hd_metrics(OutputSize out) noexcept {
    struct Rung {
        std::int32_t tier;
        std::int32_t hero, title, section, body, caption, small;
        std::int32_t radius_md, radius_sm;
    };

    constexpr Rung kLadder[] = {
        {240, 14, 12, 11, 10, 9, 8, 2, 1},
        {480, 22, 18, 17, 16, 14, 13, 3, 2},
        {540, 24, 20, 18, 17, 15, 14, 4, 2},
        {720, 29, 23, 21, 19, 17, 16, 6, 3},
        {1080, 43, 35, 31, 28, 26, 24, 8, 4},
    };
    constexpr std::size_t kDefaultRung = 3;

    constexpr std::int32_t kBaseTier = 720, kHalf = 2;

    constexpr std::int32_t kPadRight = 28, kHeaderGap = 4, kListTop = 116, kHelpPad = 16,
                           kHelpBar = 56, kGap = 12, kRowH = 42, kRowPad = 12, kCardPad = 7,
                           kGlyph = 24, kIcon = 18, kButtonH = 56, kTrackH = 8;

    constexpr std::uint8_t kInsetPct = 5;
    constexpr std::int32_t kMille = 1000, kMilleRound = kMille / 2, kStrokeMille = 2,
                           kFocusMille = 4, kRingMille = 6, kPressMille = 8;
    HdMetrics m{};
    m.tier = hd_tier(out.h);
    Rung r = kLadder[kDefaultRung];
    for (const Rung& x : kLadder)
        if (x.tier == m.tier)
            r = x;
    const auto k = [t = m.tier](std::int32_t base720) {
        return (base720 * t + kBaseTier / kHalf) / kBaseTier;
    };
    const std::int32_t ih = out.bounds().h;
    m.font_hero = r.hero;
    m.font_title = r.title;
    m.font_section = r.section;
    m.font_body = r.body;
    m.font_caption = r.caption;
    m.font_small = r.small;
    m.radius_md = r.radius_md;
    m.radius_sm = r.radius_sm;
    const HdRect insets =
        OutputSize{
            .w = out.w.percent_rounded(Percent::of(kInsetPct)),
            .h = out.h.percent_rounded(Percent::of(kInsetPct))
        }
            .bounds();
    m.inset_x = insets.w;
    m.inset_y = insets.h;
    m.pad_right = k(kPadRight);
    m.header_gap = k(kHeaderGap);
    m.list_top = m.inset_y + k(kListTop);
    m.help_pad = k(kHelpPad);
    m.help_top = ih - (m.inset_y + k(kHelpBar));
    m.gap = k(kGap);
    m.row_h = k(kRowH);
    m.row_pad = k(kRowPad);
    m.card_pad = k(kCardPad);
    m.stroke = std::max<std::int32_t>(1, (ih * kStrokeMille + kMilleRound) / kMille);
    m.focus_stroke = std::max<std::int32_t>(1, (ih * kFocusMille + kMilleRound) / kMille);
    m.ring_w = std::max<std::int32_t>(1, (ih * kRingMille + kMilleRound) / kMille);
    m.press_edge = std::max<std::int32_t>(1, (ih * kPressMille + kMilleRound) / kMille);
    m.glyph = k(kGlyph);
    m.icon = k(kIcon);
    m.button_h = k(kButtonH);
    m.track_h = k(kTrackH);
    return m;
}

[[nodiscard]] constexpr HdMetrics hd_under_band(HdMetrics m, std::int32_t band_h) noexcept {
    if (band_h <= 0)
        return m;
    m.list_top = m.list_top - m.inset_y + m.gap;
    m.inset_y = m.gap;
    m.help_top -= band_h;
    return m;
}

[[nodiscard]] constexpr HdMetrics hd_snap_type(HdMetrics m, std::uint8_t em) noexcept {
    if (em == 0)
        return m;
    const std::int32_t e = em;
    constexpr std::int32_t kHalves = 2, kBodyRowNum = 3, kBodyRowDen = 4;
    const auto snap = [e](std::int32_t v) {
        const std::int32_t q = v / e + (kHalves * (v % e) > e ? 1 : 0);
        return std::max<std::int32_t>(1, q) * e;
    };
    const std::int32_t body_up = std::max<std::int32_t>(1, (m.font_body + e - 1) / e);
    const std::int32_t body_fits = std::max<std::int32_t>(
        1,
        m.row_h * kBodyRowNum / kBodyRowDen / e
    );
    m.font_hero = snap(m.font_hero);
    m.font_section = snap(m.font_section);
    m.font_body = std::min(body_up, body_fits) * e;
    m.font_title = m.font_body + e;
    m.font_caption = snap(m.font_caption);
    m.font_small = snap(m.font_small);
    return m;
}

[[nodiscard]] constexpr std::int32_t hd_art_pixel(
    const HdMetrics& snapped,
    std::uint8_t em
) noexcept {
    constexpr std::int32_t kBevelThirds = 3;
    if (em == 0)
        return 1;
    const std::int32_t frame = (snapped.card_pad + snapped.stroke) / kBevelThirds;
    return std::max<std::int32_t>(1, std::min<std::int32_t>(snapped.font_body / em, frame));
}

}
