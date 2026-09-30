// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct PcmFeedState {

    std::uint32_t write_point = 0;
    bool playing = false;

    bool primed = false;

    bool drained = false;

    std::uint32_t served = 0;
};

}  // namespace mister::cores
