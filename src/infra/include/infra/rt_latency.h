// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/indexed_array.h"
#include "infra/lat_hist.h"
#include "infra/seat.h"
#include "infra/service_slot.h"

namespace mister::infra {

struct alignas(64) RtLatency {
    TASTY_SEAT_EXEMPT(component);

    infra::IndexedArray<LatHist, kMaxServices, ServiceSlot> service_latency{};
    LatHist tick_jitter;
};

}
