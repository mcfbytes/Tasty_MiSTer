// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct FrameDemand {
    std::uint8_t on = 0;
    std::uint8_t pad_[3]{};
};

using FrameDemandCell = xthread::Telemetry<FrameDemand, SeatTag::Capture>;

}  // namespace mister::app
