// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace mister::ui {

enum class KeyGridLayer : std::uint8_t {
    Lower,
    Upper,
    Symbols,
    Digits,
};

[[nodiscard]] constexpr KeyGridLayer next_layer(KeyGridLayer layer) noexcept {
    switch (layer) {
        case KeyGridLayer::Lower:
            return KeyGridLayer::Upper;
        case KeyGridLayer::Upper:
            return KeyGridLayer::Symbols;
        case KeyGridLayer::Symbols:
            return KeyGridLayer::Lower;
        case KeyGridLayer::Digits:
            return KeyGridLayer::Digits;
    }
    return KeyGridLayer::Lower;
}

[[nodiscard]] constexpr std::string_view layer_label(KeyGridLayer layer) noexcept {
    switch (layer) {
        case KeyGridLayer::Lower:
            return "abc";
        case KeyGridLayer::Upper:
            return "ABC";
        case KeyGridLayer::Symbols:
            return "!@#";
        case KeyGridLayer::Digits:
            return "123";
    }
    return {};
}

static_assert(std::is_trivially_copyable_v<KeyGridLayer>);

}
