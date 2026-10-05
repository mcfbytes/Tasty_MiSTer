// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_key_bridge.h"

#include <linux/input-event-codes.h>

#include <array>

#include "proto/joystick.h"

namespace mister::app {

namespace {

struct PadKey {
    std::uint32_t bit;
    std::uint16_t code;
};

constexpr std::array<PadKey, 12> kPadKeys{{
    {proto::kJoyUp, KEY_UP},
    {proto::kJoyDown, KEY_DOWN},
    {proto::kJoyLeft, KEY_LEFT},
    {proto::kJoyRight, KEY_RIGHT},
    {proto::kJoyBtn1, KEY_ENTER},
    {proto::kJoyBtn2, KEY_ESC},
    {proto::kJoyBtn3, KEY_SPACE},
    {proto::kJoyBtn4, KEY_TAB},
    {proto::kJoyL, KEY_PAGEUP},
    {proto::kJoyR, KEY_PAGEDOWN},
    {proto::kJoyL2, KEY_F1},
    {proto::kJoyR2, KEY_BACKSPACE},
}};

}  // namespace

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
    if (!on && active_) release_all_();

    if (on && !active_) pad_or_ = pad_any_();
    active_ = on;
    return active_;
}

std::uint32_t LauncherKeyBridge::pad_any_() const noexcept {
    std::uint32_t any = 0;
    for (const std::uint32_t l : pad_level_)
        any |= l;
    return any;
}

void LauncherKeyBridge::sync_(std::uint16_t code) noexcept {
    if (code == 0 || code >= 256) return;
    const std::uint64_t bit = std::uint64_t{1} << (code % 64);
    bool want = (kbd_down_[code / 64] & bit) != 0;
    for (const PadKey& k : kPadKeys)
        want = want || (k.code == code && (pad_or_ & k.bit) != 0);
    std::uint64_t& sent = down_[code / 64];
    if (((sent & bit) != 0) == want) return;
    sent = want ? (sent | bit) : (sent & ~bit);
    out_.key(code, want);
}

void LauncherKeyBridge::key(std::uint16_t code, bool down) noexcept {
    TASTY_SEAT_BODY(LauncherKeyBridge);
    if (!active_ || code == 0 || code >= 256) return;
    std::uint64_t& word = kbd_down_[code / 64];
    const std::uint64_t bit = std::uint64_t{1} << (code % 64);
    word = down ? (word | bit) : (word & ~bit);
    sync_(code);
    ++n_.keys;
}

void LauncherKeyBridge::pad(std::size_t slot, std::uint32_t level) noexcept {
    TASTY_SEAT_BODY(LauncherKeyBridge);
    if (slot >= pad_level_.size()) return;
    pad_level_[slot] = level;
    if (!active_) return;
    const std::uint32_t any = pad_any_();
    const std::uint32_t diff = any ^ pad_or_;
    pad_or_ = any;
    for (const PadKey& k : kPadKeys) {
        if ((diff & k.bit) == 0) continue;
        sync_(k.code);
        ++n_.pad_keys;
    }
}

void LauncherKeyBridge::reseat_pad(std::size_t slot, std::uint32_t level) noexcept {
    TASTY_SEAT_BODY(LauncherKeyBridge);
    if (slot >= pad_level_.size()) return;
    pad_level_[slot] = level;
    if (!active_) return;
    const std::uint32_t any = pad_any_();
    const std::uint32_t gone = pad_or_ & ~any;
    pad_or_ = any;
    for (const PadKey& k : kPadKeys)
        if ((gone & k.bit) != 0) sync_(k.code);
}

void LauncherKeyBridge::release_all_() noexcept {
    for (std::uint16_t w = 0; w < 4; ++w) {
        kbd_down_[w] = 0;
        while (down_[w] != 0) {
            const auto b = static_cast<std::uint16_t>(__builtin_ctzll(down_[w]));
            down_[w] &= down_[w] - 1;
            out_.key(static_cast<std::uint16_t>(w * 64 + b), false);
            ++n_.releases;
        }
    }
}

}  // namespace mister::app
