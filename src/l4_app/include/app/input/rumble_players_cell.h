// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "svc/types.h"

namespace mister::app::input {

using RumblePlayersCell = xthread::Telemetry<svc::PlayerMask, SeatTag::Input>;

}
