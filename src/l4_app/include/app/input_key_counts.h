// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/frame_drops.h"
#include "svc/types.h"

namespace mister::app {

struct InputKeyCounts {
    proto::FrameDrops keys_dropped{};
    svc::Tally key_sweeps{};
    svc::Tally keys_released{};
    svc::Tally menu_keys{};
    svc::Tally menu_keys_dropped{};
};

}
