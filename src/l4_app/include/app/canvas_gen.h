// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct CanvasGen {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr CanvasGen next() const noexcept { return CanvasGen{v + 1u}; }

    constexpr bool operator==(const CanvasGen&) const noexcept = default;
};

}
