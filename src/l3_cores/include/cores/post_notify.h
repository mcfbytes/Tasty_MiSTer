// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::cores {

struct PostNotify {
    static constexpr std::uint8_t kIndex = 10;

    static constexpr std::uint8_t kCopyWord = 4;
    bool armed = false;
    std::array<std::uint16_t, 5> words{};

    [[nodiscard]] constexpr bool copies() const noexcept {
        return armed && (words[0] & 0x8000u) == 0u && words[kCopyWord - 1u] != 0u;
    }

    [[nodiscard]] constexpr std::uint32_t copy_bytes() const noexcept {
        return copies() ? words[1] | (std::uint32_t{words[2]} << 16) : 0u;
    }
};

}  // namespace mister::cores
