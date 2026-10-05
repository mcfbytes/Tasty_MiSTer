// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct LauncherFbAck {
    std::uint32_t screen_gen = 0;
    bool held = false;
};

using LauncherFbAckCell = xthread::Telemetry<LauncherFbAck, SeatTag::Ui>;

}  // namespace mister::app
