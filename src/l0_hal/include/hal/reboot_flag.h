// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

struct RebootFlag {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint32_t word() const noexcept { return v; }

    [[nodiscard]] constexpr bool warm() const noexcept { return v == kWarm; }

    friend constexpr bool operator==(RebootFlag, RebootFlag) = default;

    static constexpr std::uint32_t kWarm = 0xBEEF'B001u;
};

}
