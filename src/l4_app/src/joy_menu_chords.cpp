// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/joy_menu_chords.h"

#include <optional>

#include "proto/joystick_port.h"

namespace mister::app {

namespace {

constexpr svc::EvdevCode kKeyRight{106};
constexpr svc::EvdevCode kKeyLeft{105};
constexpr svc::EvdevCode kKeyUp{103};
constexpr svc::EvdevCode kKeyDown{108};
constexpr svc::EvdevCode kKeyEnter{28};
constexpr svc::EvdevCode kKeyBackspace{14};
constexpr svc::EvdevCode kKeyTab{15};
constexpr svc::EvdevCode kKeyMinus{12};
constexpr svc::EvdevCode kKeyEqual{13};
constexpr svc::EvdevCode kKeyGrave{41};
constexpr svc::EvdevCode kKeyBack{158};

constexpr svc::JoyMask kJoyRight{proto::kJoyRight};
constexpr svc::JoyMask kJoyLeft{proto::kJoyLeft};
constexpr svc::JoyMask kJoyUp{proto::kJoyUp};
constexpr svc::JoyMask kJoyDown{proto::kJoyDown};
constexpr svc::JoyMask kJoyBtn1{proto::kJoyBtn1};
constexpr svc::JoyMask kJoyBtn2{proto::kJoyBtn2};
constexpr svc::JoyMask kJoyBtn3{proto::kJoyBtn3};
constexpr svc::JoyMask kJoyBtn4{proto::kJoyBtn4};
constexpr svc::JoyMask kJoyL{proto::kJoyL};
constexpr svc::JoyMask kJoyR{proto::kJoyR};
constexpr svc::JoyMask kJoyR2{proto::kJoyR2};

constexpr svc::JoyMask kJoyChord = kJoyBtn1.with(kJoyBtn2);
constexpr svc::JoyMask kJoyChordTriple = kJoyChord.with(kJoyBtn3);

std::optional<svc::EvdevCode> joy_menu_key(svc::JoyMask mask) noexcept {
    if (mask == kJoyRight)
        return kKeyRight;
    if (mask == kJoyLeft)
        return kKeyLeft;
    if (mask == kJoyUp)
        return kKeyUp;
    if (mask == kJoyDown)
        return kKeyDown;
    if (mask == kJoyBtn1)
        return kKeyEnter;
    if (mask == kJoyBtn2)
        return kKeyBack;
    if (mask == kJoyBtn3)
        return kKeyBackspace;
    if (mask == kJoyBtn4)
        return kKeyTab;
    if (mask == kJoyL)
        return kKeyMinus;
    if (mask == kJoyR)
        return kKeyEqual;
    if (mask == kJoyR2)
        return kKeyGrave;
    return std::nullopt;
}

bool is_ini_direction(svc::JoyMask mask) noexcept {
    return mask == kJoyRight || mask == kJoyLeft || mask == kJoyUp || mask == kJoyDown;
}

}

void JoyMenuChords::publish_joy_menu(
    const svc::DeviceReport& r,
    svc::DeviceSlot slot,
    KeyFocus focus,
    bool suppressed,
    svc::JoyMask& prev_menu,
    InputDecode& lane,
    MenuEdgeSink sink
) {
    const bool osd_visible = focus != KeyFocus::Core;
    const svc::JoyMask now = r.menu_buttons;
    const svc::JoyMask prev = prev_menu;
    prev_menu = now;
    if (launcher_keys_)
        launcher_keys_->pad(slot, now);

    if (suppressed || !osd_visible) {
        osdbtn_ = {};
        return;
    }

    svc::JoyMask diff = now.differing(prev);
    while (!diff.empty()) {
        const svc::JoyMask bit = diff.lowest();
        diff = diff.without(bit);
        const bool press = now.any(bit);

        svc::JoyMask mask = bit.without(kJoyBtn3);
        if (press) {
            osdbtn_ = osdbtn_.with(mask);
            if (mask.any(kJoyChord) && osdbtn_.only(kJoyChord) == kJoyChord) {
                osdbtn_ = osdbtn_.with(kJoyBtn3);
                mask = kJoyBtn3;
            }
        } else {
            const svc::JoyMask old = osdbtn_;
            osdbtn_ = osdbtn_.without(mask);
            if (mask.any(kJoyChord)) {
                if (old.only(kJoyChordTriple) == kJoyChordTriple) {
                    mask = kJoyBtn3;
                } else if (old.any(kJoyBtn3)) {
                    if (!osdbtn_.any(kJoyChord))
                        osdbtn_ = osdbtn_.without(kJoyBtn3);
                    mask = {};
                }
            }
            if (mask.any(kJoyBtn2) && !old.any(kJoyBtn2))
                mask = {};
        }

        if (press
            && focus == KeyFocus::MenuIniSwitch
            && osdbtn_.any(kJoyBtn2)
            && is_ini_direction(mask)) {
            const std::optional<svc::EvdevCode> dir = joy_menu_key(mask);
            if (!dir)
                continue;
            osdbtn_ = {};
            if (!sink(lane, RawKeyEdge{*dir, true, KeyIntent::IniSwitch}))
                break;
            continue;
        }

        const std::optional<svc::EvdevCode> code = joy_menu_key(mask);
        if (!code)
            continue;

        if (!sink(lane, RawKeyEdge{*code, press, KeyIntent::Key}))
            break;
    }
}

}
