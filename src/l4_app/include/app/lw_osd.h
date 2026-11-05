// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/types.h"
#include "hal/osd_block.h"
#include "hal/register_window.h"
#include "infra/seat.h"
#include "proto/osd_block_decl.h"

namespace mister::app {

struct LwOsd {
    TASTY_SEAT_EXEMPT(component);
    hal::RegisterWindow<hal::OsdBlock> regs;
    proto::OsdBlockDecl block{};
    std::uint16_t gen = 0;
    FabricGeneration fabric_gen{};
    bool fabric_lost = false;
};

}
