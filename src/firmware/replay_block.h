// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/osd_close.h"
#include "app/replay/replay_control.h"
#include "app/replay/replay_feeder.h"
#include "app/replay/replay_gate.h"
#include "app/replay/replay_msg.h"
#include "app/replay_status.h"
#include "app/screenshot_pump.h"
#include "boot_parts.h"
#include "device_block.h"
#include "infra/seat.h"

namespace mister::fw {

class ReplayBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    ReplayBlock(
        const BootParts& plat,
        const DeviceBlock::Parts& parts,
        app::ScreenshotPump& shots,
        app::IOsdClose& osd
    );
    ReplayBlock(const ReplayBlock&) = delete;
    ReplayBlock& operator=(const ReplayBlock&) = delete;

    app::replay::ReplayRing replay_ring_{};
    app::replay::ReplayControlCell replay_control_{};
    app::ReplayStatusCell replay_status_{};
    app::replay::ReplayGate replay_gate_;
    app::replay::ReplayFeeder replay_feeder_;
};

}
