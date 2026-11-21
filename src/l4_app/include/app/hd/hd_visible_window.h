// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/log_clock.h"
#include "app/types.h"
#include "svc/types.h"
#include "infra/tally.h"

namespace mister::app::hd {

struct HdVisibleWindow {
    StatusMicros ask_us_min{};
    StatusMicros ask_us_p50{};
    StatusMicros ask_us_max{};
    infra::Tally ask_count{};
    infra::Tally inplace{};
    infra::Tally flips{};
    infra::Tally bd_frames{};
    KibCount copy_kb{};
};

}
