// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

enum class RegAccess : std::uint8_t { ReadOnly, WriteOnly, ReadWrite };

[[nodiscard]] constexpr bool readable(RegAccess a) noexcept {
    return a != RegAccess::WriteOnly;
}

[[nodiscard]] constexpr bool writable(RegAccess a) noexcept {
    return a != RegAccess::ReadOnly;
}

}
