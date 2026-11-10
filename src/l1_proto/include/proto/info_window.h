// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "proto/types.h"

namespace mister::proto {

inline constexpr OsdColCount kInfoMaxWidth{32};
inline constexpr OsdRowCount kInfoMaxHeight{16};
inline constexpr OsdCol kInfoOriginX{20};
inline constexpr OsdRow kInfoWindowY{10};

struct InfoWindow {
    OsdCol x{};
    OsdRow y{};
    OsdColCount width{};
    OsdRowCount height{};

    [[nodiscard]] constexpr bool empty() const noexcept {
        return width == OsdColCount{} || height == OsdRowCount{};
    }

    [[nodiscard]] constexpr std::array<std::uint16_t, 4> wire_words() const noexcept {
        return {x.wire_word(), y.wire_word(), width.wire_word(), height.wire_word()};
    }

    friend constexpr bool operator==(InfoWindow, InfoWindow) = default;
};

[[nodiscard]] inline InfoWindow info_window_for(std::string_view message) noexcept {
    std::uint16_t max_width = 0;
    std::uint16_t row = 1;
    std::uint16_t col = 1;
    for (const char c : message) {
        if (c == 0x0D)
            continue;
        if (c == 0x0A) {
            col = 1;
            ++row;
            continue;
        }
        ++col;
        if (col > max_width)
            max_width = col;
    }
    InfoWindow w{};
    w.x = kInfoOriginX;
    w.y = kInfoWindowY;
    w.width = OsdColCount::of(max_width + 1u);
    if (w.width > kInfoMaxWidth)
        w.width = kInfoMaxWidth;
    w.height = OsdRowCount::of(row + 2u);
    if (w.height > kInfoMaxHeight)
        w.height = kInfoMaxHeight;
    return w;
}

}
