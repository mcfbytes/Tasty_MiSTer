// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/types.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct CaptureArm {
    CaptureToken token{};
    bool armed = false;
};

using CaptureArmCell = xthread::Telemetry<CaptureArm, SeatTag::Ui>;

}
