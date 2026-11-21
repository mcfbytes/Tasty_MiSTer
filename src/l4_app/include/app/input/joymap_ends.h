// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input/capture_arm.h"
#include "app/input/capture_hit.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"

namespace mister::app::input {

struct JoyMapEnds {
    TASTY_SEAT_RESIDENT(Ui);
    CaptureArmCell& arm;
    const CaptureHitCell& hit;
    xthread::WakeFlag& hotplug;
    xthread::WakeFlag::Kicker input_ctrl;
};

}
