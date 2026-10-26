// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/register_window.h"
#include "hal/uio_reg_file.h"
#include "infra/seat.h"
#include "proto/axi_block_decl.h"

namespace mister::app {

struct LwPads {
    TASTY_SEAT_EXEMPT(component);
    hal::RegisterWindow<hal::UioRegFile> regs;
    proto::AxiBlockDecl block{};
};

}
