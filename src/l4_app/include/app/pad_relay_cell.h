// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/link/pad_relay.h"

namespace mister::app {

using PadRelayCell = xthread::Telemetry<proto::link::PadRelay, SeatTag::RT>;

}
