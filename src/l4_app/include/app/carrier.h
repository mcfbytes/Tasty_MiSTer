// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

enum class Carrier : std::uint8_t { Spi, Lw, Ocram };

[[nodiscard]] constexpr bool link_carries(Carrier c) noexcept {
    return c == Carrier::Spi;
}

}
