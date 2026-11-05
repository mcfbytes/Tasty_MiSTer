// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/cd_tick_periods.h"
#include "cores/manifests/megacd.h"

namespace mister::cores::manifests {

inline constexpr std::uint8_t kNeoGeoCdCrcStart = 5;

inline constexpr bool kNeoGeoCdStatusCarriesIsData = false;

inline constexpr bool kNeoGeoCdReadyProbeSkipsMode1 = false;

inline constexpr bool kNeoGeoCdStreamOnDataTrack = true;

inline constexpr CdTickPeriods kNeoGeoCdTicks{
    .idle = std::chrono::microseconds{10'000},
    .stream = std::chrono::microseconds{10'000},
    .caller = std::chrono::microseconds{1'000},
    .speeds = 4,
    .speed = {
        std::chrono::microseconds{10'000},
        std::chrono::microseconds{5'000},
        std::chrono::microseconds{4'000},
        std::chrono::microseconds{2'000}
    },
};

static_assert(
    kNeoGeoCdCrcStart != kMegaCdProfile.crc_start,
    "neogeocd.cpp:23,:39 seeds 5 where megacd.cpp:43,:77 seeds 0"
);
static_assert(
    kNeoGeoCdStatusCarriesIsData != kMegaCdProfile.status_carries_is_data,
    "megacd.cpp:46 ORs isData into word2 bit 8; neogeocd.cpp:42 does not"
);
static_assert(
    kNeoGeoCdReadyProbeSkipsMode1 != kMegaCdProfile.ready_probe_skips_mode1,
    "megacd.cpp:288-290 skips the MODE1 probe; neogeocd.cpp:160-169 makes it"
);
static_assert(
    kNeoGeoCdStreamOnDataTrack != kMegaCdProfile.stream_on_data_track,
    "megacd.cpp:32 keys the fast cadence off CD-DA; neogeocd.cpp:92 off DATA"
);
static_assert(
    kNeoGeoCdTicks.speeds == 4 && kMegaCdTicks.speeds == 0,
    "neogeocd.cpp:97-108 is the tree's only speed-indexed cadence"
);
static_assert(
    kNeoGeoCdTicks.speed[0] == kNeoGeoCdTicks.idle
        && kNeoGeoCdTicks.speed[0] == kNeoGeoCdTicks.stream,
    "neogeocd.cpp:90: `interval = 10` is the default AND the speed-0 entry"
);
static_assert(
    kNeoGeoCdTicks.speed[0] > kNeoGeoCdTicks.speed[1]
        && kNeoGeoCdTicks.speed[1] > kNeoGeoCdTicks.speed[2]
        && kNeoGeoCdTicks.speed[2] > kNeoGeoCdTicks.speed[3],
    "neogeocd.cpp:97-108: a higher speed index is a shorter interval"
);
static_assert(
    kNeoGeoCdTicks.caller < kNeoGeoCdTicks.speed[3],
    "the row must run faster than the 2 ms pass, or the gate aliases it"
);

}
