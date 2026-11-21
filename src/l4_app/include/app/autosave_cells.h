// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"

namespace mister::app {

using OsdMaskCell = xthread::Telemetry<proto::OsdMask, SeatTag::RT>;

using MenuFocusCell = xthread::Telemetry<bool, SeatTag::Ui>;

}
