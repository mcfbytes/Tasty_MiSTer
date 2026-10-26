// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

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
    std::uint32_t fabric_gen = 0;
    bool fabric_lost = false;
};

}
