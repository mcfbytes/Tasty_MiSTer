// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace mister::cores {

struct SendBracket {
    std::uint8_t index = 0;
    std::vector<std::uint8_t> bytes;
    std::uint32_t aux = 0;
};

}  // namespace mister::cores
