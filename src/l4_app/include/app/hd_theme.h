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
    std::uint32_t panel = 0;
    std::uint32_t card = 0;
    std::uint32_t border = 0;
    std::uint32_t muted = 0;
    std::uint32_t selection = 0;
    std::uint32_t on_accent = 0;
    float backdrop_dim = 0.4f;
    std::uint32_t fade_ms = 150;
    float title = 28.f / 720.f;
    float row = 20.f / 720.f;
    float pad = 28.f / 720.f;
};

[[nodiscard]] double contrast_ratio(std::uint32_t a, std::uint32_t b) noexcept;
[[nodiscard]] HdTheme derive_theme(HdStyle style, std::uint8_t fps) noexcept;

}  // namespace mister::app
