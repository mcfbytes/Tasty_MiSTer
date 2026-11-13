// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

enum class KeyFocus : std::uint8_t { Core = 0, Menu = 1, MenuIniSwitch = 2 };

using KeyFocusCell = xthread::Telemetry<KeyFocus, SeatTag::Ui>;

}
