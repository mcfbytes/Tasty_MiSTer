// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::hal {

struct CauseReg {
    enum class Reg : std::uint32_t { Cause = 0x0 };
    static constexpr std::size_t kSize = 4;

    static constexpr RegAccess kAccess = RegAccess::ReadOnly;
};

}
