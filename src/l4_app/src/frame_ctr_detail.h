// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

constexpr std::uint8_t ctr_minus(std::uint8_t c, std::uint32_t n) noexcept {
    return static_cast<std::uint8_t>((c - n) & 0x7u);
}

}
