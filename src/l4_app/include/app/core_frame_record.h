// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/types.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

struct CoreFrameRecord {
    std::uint64_t core_frame = 0;
    std::int64_t edge_ns = 0;
    std::uint32_t epoch = 0;
    CoreEdgeSeq core_seq{};
    infra::Niche<MoviePosition> movie_frame{};
    bool supported = false;
    std::uint8_t pad_[3]{};
};

using CoreFrameCell = xthread::Telemetry<CoreFrameRecord, SeatTag::RT>;

}
