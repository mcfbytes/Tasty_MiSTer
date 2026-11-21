// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/lw_grant_stamp.h"
#include "app/osd_grant.h"
#include "app/types.h"
#include "hal/osd_block.h"
#include "hal/register_window.h"
#include "infra/seat.h"
#include "proto/link/osd_block_decl.h"

namespace mister::app {

struct LwOsd {
    TASTY_SEAT_EXEMPT(component);
    using Grant = OsdGrant;
    hal::RegisterWindow<hal::OsdBlock> regs;
    proto::link::OsdBlockDecl block{};
    LwGrantStamp stamp{};

    [[nodiscard]] Grant lend() const noexcept {
        return Grant{
            {.block = regs.lend(), .gen = stamp.gen(), .body = {.rows = block.shape.rows()}}
        };
    }
};

}
