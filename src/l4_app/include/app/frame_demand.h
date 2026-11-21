// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

struct FrameDemand {
    bool on = false;
    std::uint8_t pad_[3]{};
};

using FrameDemandCell = xthread::Telemetry<FrameDemand, SeatTag::Capture>;

}
