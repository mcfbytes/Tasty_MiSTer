// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

enum class ReplayOp : std::uint8_t { None, Play, Stop };

struct ReplayControl {
    std::uint16_t gen = 0;
    ReplayOp op = ReplayOp::None;
    std::uint8_t pad_ = 0;
};

using ReplayControlCell = xthread::Telemetry<ReplayControl, SeatTag::Ui>;

}  // namespace mister::app
