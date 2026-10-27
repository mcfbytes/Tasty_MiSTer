// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "app/hd_style.h"

namespace mister::app {

struct HdThemeGlass {
    std::uint32_t ground = 0;
    std::uint32_t plate = 0;
    std::uint32_t ground_key = 0;
    std::uint32_t plate_key = 0;

    std::uint8_t opacity = 100;

    std::uint8_t under_dim = 100;

    [[nodiscard]] constexpr bool on() const noexcept { return opacity < 100; }
};

struct HdTheme {
    std::uint32_t primary = 0;
    std::uint32_t accent = 0;
    std::uint32_t text = 0;
    std::uint32_t warning = 0;
    std::uint32_t bg_bar = 0;
    std::uint32_t panel = 0;
    std::uint32_t card = 0;
    std::uint32_t border_subtle = 0;
    std::uint32_t border_mid = 0;
    std::uint32_t text_label = 0;
    std::uint32_t selection_fill = 0;
    std::uint32_t on_accent = 0;
    std::uint32_t cue_spent = 0;
    std::uint32_t control_edge = 0;
    std::uint32_t error = 0;
    std::uint32_t window = 0;
    std::uint32_t window_focus = 0;
    std::uint32_t on_window_focus = 0;
    std::uint32_t window_hand = 0;
    std::uint32_t scrim_argb = 0xcc000000u;
    float backdrop_dim = 0.4f;

    std::uint8_t backdrop_ramp_pct = 100;
    std::uint32_t select_fade_ms = 0;
    HdThemeGlass glass{};
};

[[nodiscard]] double contrast_ratio(std::uint32_t a, std::uint32_t b) noexcept;
[[nodiscard]] std::uint32_t shown_565(std::uint32_t rgb) noexcept;
[[nodiscard]] std::uint32_t settle_565(std::uint32_t rgb) noexcept;

inline constexpr int kWindowBands = 8;
[[nodiscard]] std::uint32_t window_band(std::uint32_t window, int band) noexcept;
[[nodiscard]] HdTheme derive_theme(HdStyle style) noexcept;

enum class HdGround : std::uint8_t { Primary, WindowShade };
[[nodiscard]] std::uint32_t hd_ground_fill(const HdTheme& t, HdGround ground) noexcept;

[[nodiscard]] constexpr std::uint16_t hd_pack565(std::uint32_t rgb) noexcept {
    return static_cast<std::uint16_t>(
        ((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) | ((rgb >> 3) & 0x001Fu)
    );
}

[[nodiscard]] constexpr std::uint16_t hd_scale_565(std::uint16_t c, unsigned pct) noexcept {
    const unsigned r = (c >> 11) * pct / 100u;
    const unsigned g = ((c >> 5) & 0x3Fu) * pct / 100u;
    const unsigned b = (c & 0x1Fu) * pct / 100u;
    return static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
}

[[nodiscard]] constexpr std::uint16_t hd_under_q8(std::uint8_t opacity, std::uint8_t dim) noexcept {
    const unsigned o = opacity > 100 ? 100u : opacity;
    const unsigned d = dim > 100 ? 100u : dim;
    return static_cast<std::uint16_t>((100u - o) * d * 256u / 10000u);
}

[[nodiscard]] HdThemeGlass hd_glass_keys(
    const HdTheme& t,
    HdGround ground,
    std::uint8_t opacity
) noexcept;

struct HdTint {
    std::uint32_t fill = 0;
    std::uint32_t ink = 0;
};

[[nodiscard]] HdTint hd_core_tint(const HdTheme& theme, std::string_view core) noexcept;

}
