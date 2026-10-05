// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct LauncherState {
    std::uint32_t screen_gen = 0;
    bool owns_screen = false;
    bool settled = false;
    bool scanout_owned = false;
    bool hosting = false;
};

using LauncherStateCell = xthread::Telemetry<LauncherState, SeatTag::Launcher>;

}  // namespace mister::app
