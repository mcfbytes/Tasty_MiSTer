// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct AspectRaw {
    std::uint16_t arx_raw = 0;
    std::uint16_t ary_raw = 0;
    static constexpr std::uint16_t kArxyFlag = 0x1000;
    static constexpr std::uint16_t kValueMask = 0x0FFF;

    [[nodiscard]] constexpr bool arxy() const noexcept { return (arx_raw & kArxyFlag) != 0; }

    [[nodiscard]] constexpr std::uint16_t arx() const noexcept {
        return static_cast<std::uint16_t>(arx_raw & kValueMask);
    }

    [[nodiscard]] constexpr std::uint16_t ary() const noexcept {
        return static_cast<std::uint16_t>(ary_raw & kValueMask);
    }

    friend constexpr bool operator==(AspectRaw, AspectRaw) = default;
};

}
