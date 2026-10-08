// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/osd_close.h"
#include "app/replay_control.h"
#include "app/replay_feeder.h"
#include "app/replay_gate.h"
#include "app/replay_msg.h"
#include "app/replay_status.h"
#include "app/screenshot_pump.h"
#include "boot_parts.h"
#include "device_block.h"
#include "infra/seat.h"

namespace mister::fw {

class ReplayBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    ReplayBlock(const BootParts& plat, DeviceBlock& device, app::ScreenshotPump& shots,
                app::IOsdClose& osd);
    ReplayBlock(const ReplayBlock&) = delete;
    ReplayBlock& operator=(const ReplayBlock&) = delete;

    app::ReplayRing replay_ring_{};
    app::ReplayControlCell replay_control_{};
    app::ReplayStatusCell replay_status_{};
    app::ReplayGate replay_gate_;
    app::ReplayFeeder replay_feeder_;
};

}  // namespace mister::fw
