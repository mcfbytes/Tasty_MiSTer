// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "reactor/cause_set.h"
#include "hal/types.h"
#include "os/types.h"

namespace mister::proto {

inline constexpr std::uint32_t kIrqPoolFirstLine = 1;

inline constexpr std::uint32_t kIrqOrdinalMax = 255;

struct IrqBinding {
    reactor::Cause klass;
    os::UioLine line;
    hal::LwOffset cause_base;
};

}  // namespace mister::proto
