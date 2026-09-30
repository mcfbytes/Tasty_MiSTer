// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct PcmExtent {

    std::uint64_t bytes = 0;

    std::uint64_t loop_byte = 0;
    bool loops = false;
};

}  // namespace mister::cores
