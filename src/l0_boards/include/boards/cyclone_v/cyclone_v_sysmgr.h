// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::boards::cyclone_v {

struct SysMgr {
    enum class Reg : std::uint32_t {
        FpgaIntfGrpModule = 0x28,
        WarmRamGrpEnable = 0xE0,
        EccGrpOcram = 0x144,
    };
    static constexpr std::size_t kSize = 0x200;
    static constexpr hal::RegAccess kAccess = hal::RegAccess::ReadWrite;
    static constexpr std::uint32_t kAllOff = 0x0;

    static constexpr std::uint32_t kOcramEccFlags = 0x19;
};

}
