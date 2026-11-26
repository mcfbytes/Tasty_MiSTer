// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::hal {

struct PadIdent {
    enum class Reg : std::uint32_t {
        Stat = 0x0,
        Id = 0x4,
    };
    static constexpr std::size_t kSize = 0x8;
    static constexpr RegAccess kAccess = RegAccess::ReadOnly;

    enum Ids : std::uint32_t { kId = 0x31444150u };
};

}
