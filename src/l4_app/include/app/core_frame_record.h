// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct CoreFrameRecord {
    std::uint64_t core_frame = 0;
    std::int64_t edge_ns = 0;
    std::uint32_t epoch = 0;
    std::uint32_t core_seq = 0;
    std::int32_t movie_frame = -1;
    std::uint8_t supported = 0;
    std::uint8_t pad_[3]{};
};

using CoreFrameCell = xthread::Telemetry<CoreFrameRecord, SeatTag::RT>;

}  // namespace mister::app
