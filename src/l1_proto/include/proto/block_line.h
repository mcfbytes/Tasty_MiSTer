// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "os/types.h"

namespace mister::proto {

struct BlockLine {
    os::UioLine line{};
    hal::LwOffset cause{};
    friend constexpr bool operator==(BlockLine, BlockLine) = default;
};

}
