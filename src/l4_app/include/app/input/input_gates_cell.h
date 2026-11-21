// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "svc/input_gates.h"

namespace mister::app::input {

using InputGatesCell = xthread::Telemetry<svc::InputGates, SeatTag::Input>;

}
