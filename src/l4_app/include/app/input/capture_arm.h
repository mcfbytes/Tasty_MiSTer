// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/types.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app::input {

struct CaptureArm {
    CaptureToken token{};
    bool armed = false;
};

using CaptureArmCell = xthread::Telemetry<CaptureArm, SeatTag::Ui>;

}
