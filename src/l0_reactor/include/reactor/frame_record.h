// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::reactor {

struct FrameRecord {
    hal::FrameSeq seq{};
};

using FrameCell = xthread::Telemetry<FrameRecord, SeatTag::Frame>;

}
