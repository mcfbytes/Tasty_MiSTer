// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/screen_gen.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

struct LauncherState {
    TASTY_SEAT_EXEMPT(const_shared);

    ScreenGen screen_gen{};
    bool owns_screen = false;
    bool settled = false;
    bool scanout_owned = false;
    bool hosting = false;
};

using LauncherStateCell = xthread::Telemetry<LauncherState, SeatTag::Launcher>;

}
