// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/poll_ticks.h"
#include "cores/types.h"

namespace mister::cores::saturn {

inline constexpr PollTicks kSeekFloorTicks{4};
inline constexpr PollTicks kSeekCeilTicks{13};
inline constexpr SectorCount kSeekLbaPerTick{2000};
inline constexpr PollTicks::Rate kSeekRate{kSeekLbaPerTick};
inline constexpr SectorCount kSeekNearWindow{4};
inline constexpr PollTicks kSeekNearTicks{2};
inline constexpr PollTicks kSeekCrossTrackAdd{27};
inline constexpr SectorCount kSeekLongSameTrack{10'000};
inline constexpr PollTicks kSeekLongSameTrackAdd{20};
inline constexpr PollTicks kSeekBackwardAdd{2};

inline constexpr SectorCount kSeekOriginLead{4};

inline constexpr SectorCount kReadHeadLead{4};

[[nodiscard]] constexpr PollTicks seek_ticks(SectorCount diff, bool cross_track) noexcept {
    const SectorCount mag = diff.magnitude();

    PollTicks n = PollTicks::of_travel(diff, kSeekRate);
    if (mag <= kSeekNearWindow) {
        n = kSeekNearTicks;
    } else if (n <= kSeekFloorTicks) {
        n = kSeekFloorTicks;
    } else if (n > kSeekCeilTicks) {
        n = kSeekCeilTicks;
    }

    if (cross_track) {
        n += kSeekCrossTrackAdd;
    } else if (mag > kSeekLongSameTrack) {
        n += kSeekLongSameTrackAdd;
    }

    if (diff < SectorCount{} - kSeekNearWindow)
        n += kSeekBackwardAdd;

    return n;
}

[[nodiscard]] constexpr PollTicks seek_delay_ticks(HeadMove move, bool cross_track) noexcept {
    return seek_ticks(move.distance(), cross_track);
}

inline constexpr PollTicks kSeekMinTicks{2};
inline constexpr PollTicks kSeekMaxTicks{40};

static_assert(seek_ticks(SectorCount{0}, false) == kSeekMinTicks);

static_assert(seek_ticks(SectorCount{4}, false) == PollTicks{2});

static_assert(seek_ticks(SectorCount{-4}, false) == PollTicks{2});

static_assert(seek_ticks(SectorCount{5}, false) == PollTicks{4});

static_assert(seek_ticks(SectorCount{-5}, false) == PollTicks{6});

static_assert(seek_ticks(SectorCount{10'000}, false) == PollTicks{5});

static_assert(seek_ticks(SectorCount{10'001}, false) == PollTicks{25});

static_assert(seek_ticks(SectorCount{26'000}, false) == PollTicks{33});

static_assert(seek_ticks(SectorCount{30'000}, false) == PollTicks{33});
static_assert(seek_ticks(SectorCount{-30'000}, false) == PollTicks{26});

static_assert(seek_ticks(SectorCount{5}, true) == PollTicks{31});

static_assert(seek_ticks(SectorCount{-30'000}, true) == PollTicks{33});

static_assert(seek_ticks(SectorCount{26'000}, true) == kSeekMaxTicks);

}
