// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/lw_grant_stamp.h"
#include "app/sys_grant.h"
#include "app/types.h"
#include "hal/register_window.h"
#include "hal/sys_block.h"
#include "infra/seat.h"
#include "os/uio_handle.h"
#include "proto/sys_block_decl.h"

namespace mister::app {

struct LwSys {
    TASTY_SEAT_EXEMPT(component);
    using Grant = SysGrant;

    static constexpr bool kHasEnable = false;
    hal::RegisterWindow<hal::SysBlock> regs;
    os::UioHandle line{};
    proto::SysBlockDecl block{};
    LwGrantStamp stamp{};

    [[nodiscard]] Grant lend() const noexcept {
        return Grant{.block = regs.lend(), .line = line.borrow(), .gen = stamp.gen()};
    }

    void disable() noexcept {}
};

}
