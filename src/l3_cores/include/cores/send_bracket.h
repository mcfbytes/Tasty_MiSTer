// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "proto/types.h"

namespace mister::cores {

struct SendBracket {
    proto::WideIoIndex index{};
    std::vector<std::uint8_t> bytes;
    std::uint32_t aux = 0;
};

}
