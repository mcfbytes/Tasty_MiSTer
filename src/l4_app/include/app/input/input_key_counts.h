// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/frame_drops.h"
#include "svc/types.h"
#include "infra/tally.h"

namespace mister::app::input {

struct InputKeyCounts {
    proto::FrameDrops keys_dropped{};
    infra::Tally key_sweeps{};
    infra::Tally keys_released{};
    infra::Tally menu_keys{};
    infra::Tally menu_keys_dropped{};
};

}
