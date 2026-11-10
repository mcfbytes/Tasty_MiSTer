// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_key_bridge.h"

#include <linux/input-event-codes.h>

#include <array>
#include <cstddef>
#include <limits>

#include "app/input_wire.h"
#include "proto/joystick_port.h"

namespace mister::app {

namespace {

struct PadKey {
    svc::JoyMask bit;
    svc::EvdevCode code;
};

constexpr std::array<PadKey, 12> kPadKeys{{
    {svc::JoyMask{proto::kJoyUp}, svc::EvdevCode{KEY_UP}},
    {svc::JoyMask{proto::kJoyDown}, svc::EvdevCode{KEY_DOWN}},
    {svc::JoyMask{proto::kJoyLeft}, svc::EvdevCode{KEY_LEFT}},
    {svc::JoyMask{proto::kJoyRight}, svc::EvdevCode{KEY_RIGHT}},
    {svc::JoyMask{proto::kJoyBtn1}, svc::EvdevCode{KEY_ENTER}},
    {svc::JoyMask{proto::kJoyBtn2}, svc::EvdevCode{KEY_ESC}},
    {svc::JoyMask{proto::kJoyBtn3}, svc::EvdevCode{KEY_SPACE}},
    {svc::JoyMask{proto::kJoyBtn4}, svc::EvdevCode{KEY_TAB}},
    {svc::JoyMask{proto::kJoyL}, svc::EvdevCode{KEY_PAGEUP}},
    {svc::JoyMask{proto::kJoyR}, svc::EvdevCode{KEY_PAGEDOWN}},
    {svc::JoyMask{proto::kJoyL2}, svc::EvdevCode{KEY_F1}},
    {svc::JoyMask{proto::kJoyR2}, svc::EvdevCode{KEY_BACKSPACE}},
}};

}

bool LauncherKeyBridge::set_active(bool on) noexcept {
    TASTY_SEAT_BODY(LauncherKeyBridge);
    if (on && !opened_) {
        if (out_.open()) {
            opened_ = true;
        } else {
            ++n_.open_failures;
            on = false;
        }
    }
    if (!on && active_)
        release_all_();

    if (on && !active_)
        pad_or_ = pad_any_();
    active_ = on;
    return active_;
}

svc::JoyMask LauncherKeyBridge::pad_any_() const noexcept {
    svc::JoyMask any{};
    for (const svc::JoyMask level : pad_level_)
        any = any.with(level);
    return any;
}

void LauncherKeyBridge::sync_(svc::EvdevCode code) noexcept {
    if (code == svc::EvdevCode{} || !code.is_key())
        return;
    bool want = held_.has(code);
    for (const PadKey& k : kPadKeys)
        want = want || (k.code == code && pad_or_.any(k.bit));
    if (sent_.has(code) == want)
        return;
    if (want) {
        sent_.set(code);
    } else {
        sent_.clear(code);
    }
    inject_(code, want);
}

void LauncherKeyBridge::inject_(svc::EvdevCode code, bool down) noexcept {
    out_.key(code.v, down);
}

void LauncherKeyBridge::key(svc::EvdevCode code, bool down) noexcept {
    TASTY_SEAT_BODY(LauncherKeyBridge);
    if (!active_ || code == svc::EvdevCode{} || !code.is_key())
        return;
    if (down) {
        held_.set(code);
    } else {
        held_.clear(code);
    }
    sync_(code);
    ++n_.keys;
}

void LauncherKeyBridge::pad(svc::DeviceSlot slot, svc::JoyMask level) noexcept {
    TASTY_SEAT_BODY(LauncherKeyBridge);
    const auto cell = pad_level_.find(slot);
    if (!cell)
        return;
    *cell = level;
    if (!active_)
        return;
    const svc::JoyMask any = pad_any_();
    const svc::JoyMask diff = any.differing(pad_or_);
    pad_or_ = any;
    for (const PadKey& k : kPadKeys) {
        if (!diff.any(k.bit))
            continue;
        sync_(k.code);
        ++n_.pad_keys;
    }
}

void LauncherKeyBridge::reseat_pad(svc::DeviceSlot slot, svc::JoyMask level) noexcept {
    TASTY_SEAT_BODY(LauncherKeyBridge);
    const auto cell = pad_level_.find(slot);
    if (!cell)
        return;
    *cell = level;
    if (!active_)
        return;
    const svc::JoyMask any = pad_any_();
    const svc::JoyMask gone = pad_or_.without(any);
    pad_or_ = any;
    for (const PadKey& k : kPadKeys)
        if (gone.any(k.bit))
            sync_(k.code);
}

void LauncherKeyBridge::release_all_() noexcept {
    sent_.drain([this](svc::EvdevCode code) {
        held_.clear(code);
        inject_(code, false);
        ++n_.releases;
    });
    held_ = {};
}

}
