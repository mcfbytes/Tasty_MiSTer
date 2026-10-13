// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/lat_hist.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"

namespace mister::xthread {

struct alignas(64) RtLatency {
    TASTY_SEAT_EXEMPT(component);
    LatHist service_latency[kMaxServices];
    LatHist tick_jitter;
    LatHist doorbell_wake[kMaxLines];
};

}  // namespace mister::xthread
