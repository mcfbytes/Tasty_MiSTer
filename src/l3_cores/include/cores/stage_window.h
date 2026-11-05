// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::cores {

struct StageWindow {
    proto::WideIoIndex index{};
    std::uint32_t address = 0;
    std::uint32_t bytes = 0;
};

}
