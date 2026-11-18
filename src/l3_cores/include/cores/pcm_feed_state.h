// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/pcm_handovers.h"
#include "cores/pcm_ring.h"

namespace mister::cores {

struct PcmFeedState {

    PcmRingPoint write_point{};
    bool playing = false;

    bool primed = false;

    bool drained = false;

    std::uint32_t served = 0;

    PcmHandovers rings_ended{};
    PcmHandovers sources_ended{};

    friend constexpr bool operator==(const PcmFeedState&, const PcmFeedState&) = default;
};

}
