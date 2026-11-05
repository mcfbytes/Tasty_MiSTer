// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "app/types.h"

namespace mister::app {

enum class HdLookField : std::uint8_t { Layout, Colors, GameDisplay, Opacity };

inline constexpr std::uint8_t kHdOpacityDefault = 85;

struct HdLook {
    HdLayoutIndex layout{};
    HdColors colors{};
    std::uint8_t display = 0;
    std::uint8_t opacity = kHdOpacityDefault;

    friend constexpr bool operator==(const HdLook&, const HdLook&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<HdLook> && sizeof(HdLook) == sizeof(std::uint32_t));

[[nodiscard]] constexpr std::uint8_t hd_look_get(const HdLook& look, HdLookField f) noexcept {
    switch (f) {
        case HdLookField::Layout:
            return look.layout.v;
        case HdLookField::Colors:
            return look.colors.v;
        case HdLookField::GameDisplay:
            return look.display;
        case HdLookField::Opacity:
            return look.opacity;
    }
    return 0;
}

[[nodiscard]] constexpr HdLook hd_look_with(HdLook look, HdLookField f, std::uint8_t v) noexcept {
    switch (f) {
        case HdLookField::Layout:
            look.layout = HdLayoutIndex{v};
            break;
        case HdLookField::Colors:
            look.colors = HdColors{v};
            break;
        case HdLookField::GameDisplay:
            look.display = v;
            break;
        case HdLookField::Opacity:
            look.opacity = v;
            break;
    }
    return look;
}

[[nodiscard]] constexpr std::string_view hd_look_ini_key(HdLookField f) noexcept {
    switch (f) {
        case HdLookField::Layout:
            return "hd_layout";
        case HdLookField::Colors:
            return "hd_colors";
        case HdLookField::GameDisplay:
            return "hd_game_display";
        case HdLookField::Opacity:
            return "hd_menu_opacity";
    }
    return {};
}

}
