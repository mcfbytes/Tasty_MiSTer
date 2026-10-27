// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace mister::app {

enum class HdLookField : std::uint8_t { Layout, Colors, GameDisplay, Opacity };

inline constexpr std::uint8_t kHdOpacityDefault = 85;

struct HdLook {
    std::uint8_t layout = 0;
    std::uint8_t colors = 0;
    std::uint8_t display = 0;
    std::uint8_t opacity = kHdOpacityDefault;

    friend constexpr bool operator==(const HdLook&, const HdLook&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<HdLook> && sizeof(HdLook) == 4);

[[nodiscard]] constexpr std::uint8_t HdLook::* hd_look_member(HdLookField f) noexcept {
    switch (f) {
        case HdLookField::Layout:
            return &HdLook::layout;
        case HdLookField::Colors:
            return &HdLook::colors;
        case HdLookField::GameDisplay:
            return &HdLook::display;
        case HdLookField::Opacity:
            return &HdLook::opacity;
    }
    return nullptr;
}

[[nodiscard]] constexpr std::uint8_t hd_look_get(const HdLook& look, HdLookField f) noexcept {
    const auto member = hd_look_member(f);
    return member != nullptr ? look.*member : std::uint8_t{0};
}

[[nodiscard]] constexpr HdLook hd_look_with(HdLook look, HdLookField f, std::uint8_t v) noexcept {
    if (const auto member = hd_look_member(f); member != nullptr)
        look.*member = v;
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
