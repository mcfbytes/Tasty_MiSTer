// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::reactor {

struct FrameRecord {
    std::uint32_t seq = 0;
};

using FrameCell = xthread::Telemetry<FrameRecord, SeatTag::Frame>;

}
