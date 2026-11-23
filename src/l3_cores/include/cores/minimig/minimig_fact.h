// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::minimig {

enum class MinimigFact : std::uint8_t {
    DetachFloppies,
    kCount,
};

[[nodiscard]] constexpr std::uint8_t fact_byte(MinimigFact f) noexcept {
    return static_cast<std::uint8_t>(f);
}

[[nodiscard]] constexpr MinimigFact fact_of(std::uint8_t b) noexcept {
    return b < fact_byte(MinimigFact::kCount) ? static_cast<MinimigFact>(b) : MinimigFact::kCount;
}

}
