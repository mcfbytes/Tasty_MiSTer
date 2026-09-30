// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::hal {

struct PinLevels {
    bool core_ready = false;
    bool menu_button = false;
    bool user_button = false;
    bool hdmi_int = false;
};
static_assert(std::is_trivially_copyable_v<PinLevels>);
static_assert(std::is_nothrow_default_constructible_v<PinLevels>);

using PinLevelCell = xthread::Telemetry<PinLevels, SeatTag::RT>;

}  // namespace mister::hal
