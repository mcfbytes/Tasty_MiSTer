// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/lw_grant_stamp.h"
#include "app/sys_grant.h"
#include "app/types.h"
#include "hal/register_window.h"
#include "hal/sys_block.h"
#include "infra/seat.h"
#include "os/uio_handle.h"
#include "proto/link/sys_block_decl.h"

namespace mister::app {

struct LwSys {
    TASTY_SEAT_EXEMPT(component);
    using Grant = SysGrant;
    hal::RegisterWindow<hal::SysBlock> regs;
    os::UioHandle line{};
    proto::link::SysBlockDecl block{};
    LwGrantStamp stamp{};

    [[nodiscard]] Grant lend() const noexcept {
        return Grant{{.block = regs.lend(), .gen = stamp.gen(), .body = {.line = line.borrow()}}};
    }
};

}
