// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/screen_gen.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app::launcher {

struct LauncherFbAck {
    ScreenGen screen_gen{};
    bool held = false;
};

using LauncherFbAckCell = xthread::Telemetry<LauncherFbAck, SeatTag::Ui>;

}
