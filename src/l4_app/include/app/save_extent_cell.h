// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/save_extent.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

using SaveExtentCell = xthread::Telemetry<cores::SaveExtent, SeatTag::RT>;

}
