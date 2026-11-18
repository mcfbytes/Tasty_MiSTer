// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/types.h"

namespace mister::app {

using OsdMaskCell = xthread::Telemetry<proto::OsdMask, SeatTag::RT>;

using MenuFocusCell = xthread::Telemetry<bool, SeatTag::Ui>;

}
