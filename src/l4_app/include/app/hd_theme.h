// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/hd_style.h"

namespace mister::app {

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
    std::uint32_t scrim_argb = 0xcc000000u;
    float backdrop_dim = 0.4f;
    std::uint32_t select_fade_ms = 0;
};

[[nodiscard]] double contrast_ratio(std::uint32_t a, std::uint32_t b) noexcept;
[[nodiscard]] std::uint32_t shown_565(std::uint32_t rgb) noexcept;
[[nodiscard]] std::uint32_t settle_565(std::uint32_t rgb) noexcept;
[[nodiscard]] HdTheme derive_theme(HdStyle style) noexcept;

}
