// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::boards::cyclone_v {

struct RstMgr {
    enum class Reg : std::uint32_t {
        Ctrl = 0x04,
        BrgModReset = 0x1C,
    };
    static constexpr std::size_t kSize = 0x100;
    static constexpr hal::RegAccess kAccess = hal::RegAccess::ReadWrite;
    static constexpr std::uint32_t kOutOfReset = 0x0;
    static constexpr std::uint32_t kInReset = 0x7;

    static constexpr std::uint32_t kF2hModule = 0x4;

    static constexpr std::uint32_t kResetRequest = 0x1;
};

}
