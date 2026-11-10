// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/log_clock.h"
#include "app/types.h"
#include "svc/types.h"

namespace mister::app {

struct HdVisibleWindow {
    StatusMicros ask_us_min{};
    StatusMicros ask_us_p50{};
    StatusMicros ask_us_max{};
    svc::Tally ask_count{};
    svc::Tally inplace{};
    svc::Tally flips{};
    svc::Tally bd_frames{};
    KibCount copy_kb{};
};

}
