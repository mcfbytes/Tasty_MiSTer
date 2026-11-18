// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::hal {

struct GpiBlock {
    enum class Reg : std::uint32_t { Gpi = 0 };
    static constexpr std::size_t kSize = sizeof(std::uint32_t);
    static constexpr RegAccess kAccess = RegAccess::ReadOnly;
};

}
