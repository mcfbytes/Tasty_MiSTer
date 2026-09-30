// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/boot_ladder.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct LadderCell {
    cores::BootLadder::Report report{};
    std::uint8_t pc = 0;
    bool live = false;
};

using LadderStateCell = xthread::Telemetry<LadderCell, SeatTag::Unbound>;

}  // namespace mister::app
