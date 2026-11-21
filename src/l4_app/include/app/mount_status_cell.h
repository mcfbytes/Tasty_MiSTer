// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/mount_status.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

using MountStatusCell = xthread::Telemetry<cores::MountStatus, SeatTag::RT>;

}
