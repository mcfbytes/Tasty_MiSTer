// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/indexed_array.h"
#include "infra/lat_hist.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"

namespace mister::xthread {

struct alignas(64) RtLatency {
    TASTY_SEAT_EXEMPT(component);

    infra::IndexedArray<LatHist, kMaxServices, ServiceSlot> service_latency{};
    LatHist tick_jitter;
};

}
