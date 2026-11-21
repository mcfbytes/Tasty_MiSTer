// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"
#include "hal/uio_reg_file.h"

namespace mister::hal {

struct PadBlock : UioRegFile {
    static constexpr RegAccess kAccess = RegAccess::WriteOnly;

    enum Shape : std::uint32_t {
        kSpinnerFifoDepth = 16,
        kKeyedSlots = 24,
        kSpinnerShift = 16,
        kShape = (kSpinnerFifoDepth << kSpinnerShift) | kKeyedSlots
    };
};

struct PadIdent {
    enum class Reg : std::uint32_t {
        Stat = 0x0,
        Id = 0x4,
    };
    static constexpr std::size_t kSize = 0x8;
    static constexpr RegAccess kAccess = RegAccess::ReadOnly;

    enum Ids : std::uint32_t { kId = 0x31444150u };
};

static_assert(PadBlock::kShape == 0x00100018u, "16 spinner FIFO entries and 24 keyed slots");

}
