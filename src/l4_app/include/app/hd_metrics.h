// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace mister::app {

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

[[nodiscard]] constexpr std::int32_t hd_tier(std::uint32_t h) noexcept {
    if (h < 400)
        return 240;
    if (h < 520)
        return 480;
    if (h < 660)
        return 540;
    if (h < 900)
        return 720;
    return 1080;
}

[[nodiscard]] constexpr HdMetrics hd_metrics(std::uint32_t w, std::uint32_t h) noexcept {
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
    HdMetrics m{};
    m.tier = hd_tier(h);
    Rung r = kLadder[3];
    for (const Rung& x : kLadder)
        if (x.tier == m.tier)
            r = x;
    const auto k = [t = m.tier](std::int32_t base720) {
        return (base720 * t + 360) / 720;
    };
    const auto ih = static_cast<std::int32_t>(h);
    m.font_hero = r.hero;
    m.font_title = r.title;
    m.font_section = r.section;
    m.font_body = r.body;
    m.font_caption = r.caption;
    m.font_small = r.small;
    m.radius_md = r.radius_md;
    m.radius_sm = r.radius_sm;
    m.inset_x = static_cast<std::int32_t>((static_cast<std::uint64_t>(w) * 5u + 50u) / 100u);
    m.inset_y = static_cast<std::int32_t>((static_cast<std::uint64_t>(h) * 5u + 50u) / 100u);
    m.pad_right = k(28);
    m.header_gap = k(4);
    m.list_top = m.inset_y + k(116);
    m.help_pad = k(16);
    m.help_top = ih - (m.inset_y + k(56));
    m.gap = k(12);
    m.row_h = k(42);
    m.row_pad = k(12);
    m.card_pad = k(7);
    m.stroke = std::max<std::int32_t>(1, (ih * 2 + 500) / 1000);
    m.focus_stroke = std::max<std::int32_t>(1, (ih * 4 + 500) / 1000);
    m.ring_w = std::max<std::int32_t>(1, (ih * 6 + 500) / 1000);
    m.press_edge = std::max<std::int32_t>(1, (ih * 8 + 500) / 1000);
    m.glyph = k(24);
    m.icon = k(18);
    m.button_h = k(56);
    m.track_h = k(8);
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
    const auto snap = [e](std::int32_t v) {
        const std::int32_t q = v / e + (2 * (v % e) > e ? 1 : 0);
        return std::max<std::int32_t>(1, q) * e;
    };
    const std::int32_t body_up = std::max<std::int32_t>(1, (m.font_body + e - 1) / e);
    const std::int32_t body_fits = std::max<std::int32_t>(1, m.row_h * 3 / 4 / e);
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
    if (em == 0)
        return 1;
    const std::int32_t frame = (snapped.card_pad + snapped.stroke) / 3;
    return std::max<std::int32_t>(1, std::min<std::int32_t>(snapped.font_body / em, frame));
}

}
