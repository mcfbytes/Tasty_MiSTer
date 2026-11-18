// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "os/types.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::uint32_t kIrqPoolFirstLine = 1;

inline constexpr std::uint32_t kIrqOrdinalMax = 255;

struct IrqBinding {
    IrqClass klass;
    os::UioLine line;
    hal::LwOffset cause_base;
    friend constexpr bool operator==(const IrqBinding&, const IrqBinding&) = default;
};

}
