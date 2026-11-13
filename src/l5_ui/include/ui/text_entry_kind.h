// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace mister::ui {

enum class TextEntryKind : std::uint8_t {
    Passphrase,
    HiddenSsid,
    Passkey,
    Pin,
};

[[nodiscard]] constexpr bool text_entry_numeric(TextEntryKind kind) noexcept {
    return kind == TextEntryKind::Passkey;
}

[[nodiscard]] constexpr bool text_entry_masked(TextEntryKind kind) noexcept {
    return kind == TextEntryKind::Passphrase;
}

[[nodiscard]] constexpr std::string_view text_entry_heading(TextEntryKind kind) noexcept {
    switch (kind) {
        case TextEntryKind::Passphrase:
            return "Password";
        case TextEntryKind::HiddenSsid:
            return "Network name";
        case TextEntryKind::Passkey:
            return "Passkey";
        case TextEntryKind::Pin:
            return "PIN";
    }
    return {};
}

static_assert(std::is_trivially_copyable_v<TextEntryKind>);

}
