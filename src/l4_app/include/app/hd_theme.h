// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "app/hd_style.h"
#include "app/types.h"

namespace mister::app {

struct HdThemeGlass {
    Rgb888 ground{};
    Rgb888 plate{};
    Rgb888 ground_key{};
    Rgb888 plate_key{};

    std::uint8_t opacity = 100;

    std::uint8_t under_dim = 100;

    [[nodiscard]] constexpr bool on() const noexcept { return opacity < 100; }

    [[nodiscard]] DimQ8 under_q8() const noexcept;

    [[nodiscard]] constexpr HdThemeGlass with_under(std::uint8_t dim) const noexcept {
        HdThemeGlass out = *this;
        out.under_dim = dim;
        return out;
    }
};

inline constexpr float kHdBackdropDim = 0.4f;

struct HdTheme {
    Rgb888 primary{};
    Rgb888 accent{};
    Rgb888 text{};
    Rgb888 warning{};
    Rgb888 bg_bar{};
    Rgb888 panel{};
    Rgb888 card{};
    Rgb888 border_subtle{};
    Rgb888 border_mid{};
    Rgb888 text_label{};
    Rgb888 selection_fill{};
    Rgb888 on_accent{};
    Rgb888 cue_spent{};
    Rgb888 control_edge{};
    Rgb888 error{};
    Rgb888 window{};
    Rgb888 window_focus{};
    Rgb888 on_window_focus{};
    Rgb888 window_hand{};

    std::uint32_t scrim_argb = 0xcc000000u;
    float backdrop_dim = kHdBackdropDim;

    std::uint8_t backdrop_ramp_pct = 100;
    std::uint32_t select_fade_ms = 0;
    HdThemeGlass glass{};
};

[[nodiscard]] double contrast_ratio(Rgb888 a, Rgb888 b) noexcept;
[[nodiscard]] Rgb888 shown_565(Rgb888 rgb) noexcept;
[[nodiscard]] Rgb888 settle_565(Rgb888 rgb) noexcept;

inline constexpr int kWindowBands = 8;
[[nodiscard]] Rgb888 window_band(Rgb888 window, int band) noexcept;
[[nodiscard]] HdTheme derive_theme(HdStyle style) noexcept;

enum class HdGround : std::uint8_t { Primary, WindowShade };
[[nodiscard]] Rgb888 hd_ground_fill(const HdTheme& t, HdGround ground) noexcept;

[[nodiscard]] constexpr std::uint16_t hd_pack565(Rgb888 c) noexcept {
    constexpr unsigned kRedDrop = 3;
    constexpr unsigned kGreenDrop = 2;
    constexpr unsigned kBlueDrop = 3;
    constexpr unsigned kRedPlace = 11;
    constexpr unsigned kGreenPlace = 5;
    const auto [r, g, b] = c.channels();
    return static_cast<std::uint16_t>(
        ((r >> kRedDrop) << kRedPlace) | ((g >> kGreenDrop) << kGreenPlace) | (b >> kBlueDrop)
    );
}

[[nodiscard]] constexpr std::uint16_t hd_scale_565(std::uint16_t c, unsigned pct) noexcept {
    constexpr unsigned kRedPlace = 11;
    constexpr unsigned kGreenPlace = 5;
    constexpr unsigned kGreenBits = 0x3F;
    constexpr unsigned kBlueBits = 0x1F;
    constexpr unsigned kPercent = 100;
    const unsigned r = (c >> kRedPlace) * pct / kPercent;
    const unsigned g = ((c >> kGreenPlace) & kGreenBits) * pct / kPercent;
    const unsigned b = (c & kBlueBits) * pct / kPercent;
    return static_cast<std::uint16_t>((r << kRedPlace) | (g << kGreenPlace) | b);
}

[[nodiscard]] HdThemeGlass hd_glass_keys(
    const HdTheme& t,
    HdGround ground,
    std::uint8_t opacity
) noexcept;

struct HdTint {
    Rgb888 fill{};
    Rgb888 ink{};
};

[[nodiscard]] HdTint hd_core_tint(const HdTheme& theme, std::string_view core) noexcept;

}
