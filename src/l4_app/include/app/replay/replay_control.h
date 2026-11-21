// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/types.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app::replay {

enum class ReplayOp : std::uint8_t { None, Play, Stop };

struct ReplayControl {
    ControlGeneration gen{};
    ReplayOp op = ReplayOp::None;
    std::uint8_t pad_ = 0;
};

using ReplayControlCell = xthread::Telemetry<ReplayControl, SeatTag::Ui>;

}
