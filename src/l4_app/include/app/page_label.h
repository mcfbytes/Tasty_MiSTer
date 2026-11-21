// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct PageLabel {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::uint32_t wire() const noexcept { return v; }

    friend constexpr bool operator==(PageLabel, PageLabel) = default;
};

}
