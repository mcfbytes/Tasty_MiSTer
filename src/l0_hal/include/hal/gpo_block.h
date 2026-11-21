// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::hal {

struct GpoBlock {
    enum class Reg : std::uint32_t { Gpo = 0 };
    static constexpr std::size_t kSize = sizeof(std::uint32_t);
    static constexpr RegAccess kAccess = RegAccess::ReadWrite;
};

}
