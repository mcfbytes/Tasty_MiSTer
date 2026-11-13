// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/keyboard_text.h"

#include <array>
#include <linux/input-event-codes.h>

#include "svc/types.h"

namespace mister::ui {
namespace {

struct KeyChar {
    svc::EvdevCode code;
    char plain;
    char shifted;
};

constexpr std::array<KeyChar, 48> kKeys{{
    {svc::EvdevCode{KEY_Q}, 'q', 'Q'},           {svc::EvdevCode{KEY_W}, 'w', 'W'},
    {svc::EvdevCode{KEY_E}, 'e', 'E'},           {svc::EvdevCode{KEY_R}, 'r', 'R'},
    {svc::EvdevCode{KEY_T}, 't', 'T'},           {svc::EvdevCode{KEY_Y}, 'y', 'Y'},
    {svc::EvdevCode{KEY_U}, 'u', 'U'},           {svc::EvdevCode{KEY_I}, 'i', 'I'},
    {svc::EvdevCode{KEY_O}, 'o', 'O'},           {svc::EvdevCode{KEY_P}, 'p', 'P'},
    {svc::EvdevCode{KEY_A}, 'a', 'A'},           {svc::EvdevCode{KEY_S}, 's', 'S'},
    {svc::EvdevCode{KEY_D}, 'd', 'D'},           {svc::EvdevCode{KEY_F}, 'f', 'F'},
    {svc::EvdevCode{KEY_G}, 'g', 'G'},           {svc::EvdevCode{KEY_H}, 'h', 'H'},
    {svc::EvdevCode{KEY_J}, 'j', 'J'},           {svc::EvdevCode{KEY_K}, 'k', 'K'},
    {svc::EvdevCode{KEY_L}, 'l', 'L'},           {svc::EvdevCode{KEY_Z}, 'z', 'Z'},
    {svc::EvdevCode{KEY_X}, 'x', 'X'},           {svc::EvdevCode{KEY_C}, 'c', 'C'},
    {svc::EvdevCode{KEY_V}, 'v', 'V'},           {svc::EvdevCode{KEY_B}, 'b', 'B'},
    {svc::EvdevCode{KEY_N}, 'n', 'N'},           {svc::EvdevCode{KEY_M}, 'm', 'M'},
    {svc::EvdevCode{KEY_SPACE}, ' ', ' '},       {svc::EvdevCode{KEY_1}, '1', '!'},
    {svc::EvdevCode{KEY_2}, '2', '@'},           {svc::EvdevCode{KEY_3}, '3', '#'},
    {svc::EvdevCode{KEY_4}, '4', '$'},           {svc::EvdevCode{KEY_5}, '5', '%'},
    {svc::EvdevCode{KEY_6}, '6', '^'},           {svc::EvdevCode{KEY_7}, '7', '&'},
    {svc::EvdevCode{KEY_8}, '8', '*'},           {svc::EvdevCode{KEY_9}, '9', '('},
    {svc::EvdevCode{KEY_0}, '0', ')'},           {svc::EvdevCode{KEY_MINUS}, '-', '_'},
    {svc::EvdevCode{KEY_EQUAL}, '=', '+'},       {svc::EvdevCode{KEY_LEFTBRACE}, '[', '{'},
    {svc::EvdevCode{KEY_RIGHTBRACE}, ']', '}'},  {svc::EvdevCode{KEY_SEMICOLON}, ';', ':'},
    {svc::EvdevCode{KEY_APOSTROPHE}, '\'', '"'}, {svc::EvdevCode{KEY_GRAVE}, '`', '~'},
    {svc::EvdevCode{KEY_BACKSLASH}, '\\', '|'},  {svc::EvdevCode{KEY_COMMA}, ',', '<'},
    {svc::EvdevCode{KEY_DOT}, '.', '>'},         {svc::EvdevCode{KEY_SLASH}, '/', '?'},
}};

[[nodiscard]] bool is_shift(svc::EvdevCode code) noexcept {
    return code == svc::EvdevCode{KEY_LEFTSHIFT} || code == svc::EvdevCode{KEY_RIGHTSHIFT};
}

}

ShiftState KeyboardText::shift_after(proto::KeyCode code, ShiftState now) noexcept {
    TASTY_SEAT_BODY(KeyboardText);
    if (!is_shift(svc::EvdevCode::of(code)))
        return now;
    return code.is_release() ? ShiftState::Up : ShiftState::Down;
}

std::optional<char> KeyboardText::of(proto::KeyCode code, ShiftState shift) noexcept {
    TASTY_SEAT_BODY(KeyboardText);
    if (code.is_release())
        return std::nullopt;
    const svc::EvdevCode k = svc::EvdevCode::of(code);
    const bool up = shift == ShiftState::Down;
    for (const KeyChar& key : kKeys) {
        if (key.code == k)
            return up ? key.shifted : key.plain;
    }
    return std::nullopt;
}

}
