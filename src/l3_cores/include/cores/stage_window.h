// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "proto/types.h"

namespace mister::cores {

struct StageWindow {
    proto::WideIoIndex index{};
    hal::FabricAddr address{};
    std::uint32_t bytes = 0;
};

}
