// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/types.h"

namespace mister::cores::saturn {

struct SeekTicks {
    std::int32_t v = 0;
    using affine_count = SeekTicks;

    [[nodiscard]] static constexpr SeekTicks of_distance(SectorCount diff) noexcept;
    friend constexpr bool operator==(SeekTicks, SeekTicks) = default;
    friend constexpr auto operator<=>(SeekTicks, SeekTicks) = default;
};

inline constexpr SeekTicks kSeekFloorTicks{4};
inline constexpr SeekTicks kSeekCeilTicks{13};
inline constexpr SectorCount kSeekLbaPerTick{2000};
inline constexpr SectorCount kSeekNearWindow{4};
inline constexpr SeekTicks kSeekNearTicks{2};
inline constexpr SeekTicks kSeekCrossTrackAdd{27};
inline constexpr SectorCount kSeekLongSameTrack{10'000};
inline constexpr SeekTicks kSeekLongSameTrackAdd{20};
inline constexpr SeekTicks kSeekBackwardAdd{2};

constexpr SeekTicks SeekTicks::of_distance(SectorCount diff) noexcept {
    return SeekTicks{diff.v / kSeekLbaPerTick.v};
}

inline constexpr SectorCount kSeekOriginLead{4};

inline constexpr SectorCount kReadHeadLead{4};

[[nodiscard]] constexpr SeekTicks seek_ticks(SectorCount diff, bool cross_track) noexcept {
    const SectorCount mag = diff.magnitude();

    SeekTicks n = SeekTicks::of_distance(diff);
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

[[nodiscard]] constexpr SeekTicks seek_delay_ticks(HeadMove move, bool cross_track) noexcept {
    return seek_ticks(move.distance(), cross_track);
}

inline constexpr SeekTicks kSeekMinTicks{2};
inline constexpr SeekTicks kSeekMaxTicks{40};

static_assert(seek_ticks(SectorCount{0}, false) == kSeekMinTicks);

static_assert(seek_ticks(SectorCount{4}, false) == SeekTicks{2});

static_assert(seek_ticks(SectorCount{-4}, false) == SeekTicks{2});

static_assert(seek_ticks(SectorCount{5}, false) == SeekTicks{4});

static_assert(seek_ticks(SectorCount{-5}, false) == SeekTicks{6});

static_assert(seek_ticks(SectorCount{10'000}, false) == SeekTicks{5});

static_assert(seek_ticks(SectorCount{10'001}, false) == SeekTicks{25});

static_assert(seek_ticks(SectorCount{26'000}, false) == SeekTicks{33});

static_assert(seek_ticks(SectorCount{30'000}, false) == SeekTicks{33});
static_assert(seek_ticks(SectorCount{-30'000}, false) == SeekTicks{26});

static_assert(seek_ticks(SectorCount{5}, true) == SeekTicks{31});

static_assert(seek_ticks(SectorCount{-30'000}, true) == SeekTicks{33});

static_assert(seek_ticks(SectorCount{26'000}, true) == kSeekMaxTicks);

}
