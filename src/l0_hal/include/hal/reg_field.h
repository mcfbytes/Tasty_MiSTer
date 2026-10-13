// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg.h"

namespace mister::hal::reg {

template <Access A, std::uint32_t Off>
struct Field {
    static_assert(Off % 4u == 0u, "a register field is dword-aligned");
    static constexpr Access kAccess = A;
    static constexpr std::uint32_t kOffset = Off;
};

}  // namespace mister::hal::reg
