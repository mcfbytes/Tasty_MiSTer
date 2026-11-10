// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

struct SdramCfg {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr std::uint16_t cookie() const noexcept {
        return static_cast<std::uint16_t>(kCookiePresent | v);
    }

    friend constexpr bool operator==(SdramCfg, SdramCfg) = default;

    static constexpr std::uint16_t kCookiePresent = 0x8000;
};

}
