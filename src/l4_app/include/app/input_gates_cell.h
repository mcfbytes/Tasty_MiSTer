// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "svc/input_gates.h"

namespace mister::app {

using InputGatesCell = xthread::Telemetry<svc::InputGates, SeatTag::Input>;

}
