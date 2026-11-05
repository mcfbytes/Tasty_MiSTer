// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/types.h"
#include "hal/register_window.h"
#include "hal/sys_block.h"
#include "infra/seat.h"
#include "os/uio_handle.h"
#include "proto/sys_block_decl.h"

namespace mister::app {

struct LwSys {
    TASTY_SEAT_EXEMPT(component);
    hal::RegisterWindow<hal::SysBlock> regs;
    os::UioHandle line{};
    proto::SysBlockDecl block{};
    std::uint16_t gen = 0;
    FabricGeneration fabric_gen{};
    bool fabric_lost = false;
};

}
