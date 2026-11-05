// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::app {

struct FlipBuffer {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr FlipBuffer other() const noexcept {
        return FlipBuffer{static_cast<std::uint8_t>(v ^ 1u)};
    }

    template <class T>
    [[nodiscard]] constexpr const T& in(const std::array<T, 2>& table) const noexcept {
        return table[v];
    }

    constexpr bool operator==(const FlipBuffer&) const noexcept = default;
};

inline constexpr std::array<FlipBuffer, 2> kFlipBuffers{FlipBuffer{0}, FlipBuffer{1}};

}
