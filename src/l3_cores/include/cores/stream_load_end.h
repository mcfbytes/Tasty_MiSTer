// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::cores {

struct StreamLoadEnd {
    proto::IoIndex index{};
    std::uint64_t bytes = 0;
    std::uint32_t crc = 0;
    bool ok = false;
};

}  // namespace mister::cores
