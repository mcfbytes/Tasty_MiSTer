// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/lw_grant_stamp.h"
#include "app/pad_grant.h"
#include "hal/register_window.h"
#include "hal/uio_reg_file.h"
#include "infra/seat.h"
#include "proto/joy_bits.h"
#include "proto/link/pad_block_decl.h"

namespace mister::app {

struct LwPad {
    TASTY_SEAT_EXEMPT(component);
    using Grant = PadGrant;
    hal::RegisterWindow<hal::UioRegFile> regs;
    proto::link::PadBlockDecl block{};
    proto::JoystickWire wire{};
    LwGrantStamp stamp{};

    [[nodiscard]] Grant lend() const noexcept {
        return Grant{{.block = regs.lend(), .gen = stamp.gen(), .body = {.wire = wire}}};
    }
};

}
