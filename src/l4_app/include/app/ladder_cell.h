// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/boot_ladder.h"
#include "cores/ladder_pc.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct LadderCell {
    cores::BootLadder::Report report{};
    cores::LadderPc pc{};
    bool live = false;
};

using LadderStateCell = xthread::Telemetry<LadderCell, SeatTag::Main>;

}
