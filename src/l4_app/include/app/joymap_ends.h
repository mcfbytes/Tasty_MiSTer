// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/capture_arm.h"
#include "app/capture_hit.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

struct JoyMapEnds {
    TASTY_SEAT_RESIDENT(Ui);
    CaptureArmCell& arm;
    const CaptureHitCell& hit;
    xthread::WakeFlag& hotplug;
    xthread::WakeFlag::Kicker input_ctrl;
};

}
