// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "svc/types.h"

namespace mister::app {

using RumblePlayersCell = xthread::Telemetry<svc::PlayerMask, SeatTag::Input>;

}
