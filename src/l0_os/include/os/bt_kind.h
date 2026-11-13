// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace mister::os {

enum class BtKind : std::uint8_t {
    Gamepad,
    Joystick,
    Keyboard,
    Mouse,
    Tablet,
    Audio,
    Phone,
    Computer,
    Other,
};

inline constexpr std::string_view kIconGaming{"input-gaming"};
inline constexpr std::string_view kIconJoystick{"input-joystick"};
inline constexpr std::string_view kIconKeyboard{"input-keyboard"};
inline constexpr std::string_view kIconMouse{"input-mouse"};
inline constexpr std::string_view kIconTablet{"input-tablet"};
inline constexpr std::string_view kIconAudioPrefix{"audio-"};
inline constexpr std::string_view kIconPhone{"phone"};
inline constexpr std::string_view kIconComputer{"computer"};

[[nodiscard]] constexpr BtKind bt_kind_from_icon(std::string_view icon) noexcept {
    if (icon == kIconGaming)
        return BtKind::Gamepad;
    if (icon == kIconJoystick)
        return BtKind::Joystick;
    if (icon == kIconKeyboard)
        return BtKind::Keyboard;
    if (icon == kIconMouse)
        return BtKind::Mouse;
    if (icon == kIconTablet)
        return BtKind::Tablet;
    if (icon.starts_with(kIconAudioPrefix))
        return BtKind::Audio;
    if (icon == kIconPhone)
        return BtKind::Phone;
    if (icon == kIconComputer)
        return BtKind::Computer;
    return BtKind::Other;
}

[[nodiscard]] constexpr bool bt_kind_is_input(BtKind kind) noexcept {
    switch (kind) {
        case BtKind::Gamepad:
        case BtKind::Joystick:
        case BtKind::Keyboard:
        case BtKind::Mouse:
        case BtKind::Tablet:
            return true;
        case BtKind::Audio:
        case BtKind::Phone:
        case BtKind::Computer:
        case BtKind::Other:
            return false;
    }
    return false;
}

static_assert(std::is_trivially_copyable_v<BtKind>);
static_assert(std::has_unique_object_representations_v<BtKind>);

}
