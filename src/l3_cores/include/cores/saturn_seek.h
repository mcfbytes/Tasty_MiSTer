// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::saturn {

inline constexpr std::int64_t kSeekFloorTicks = 4;
inline constexpr std::int64_t kSeekCeilTicks = 13;
inline constexpr std::int64_t kSeekLbaPerTick = 2000;
inline constexpr std::int64_t kSeekNearWindow = 4;
inline constexpr std::int64_t kSeekNearTicks = 2;
inline constexpr std::int64_t kSeekCrossTrackAdd = 27;
inline constexpr std::int64_t kSeekLongSameTrack = 10'000;
inline constexpr std::int64_t kSeekLongSameTrackAdd = 20;
inline constexpr std::int64_t kSeekBackwardAdd = 2;

inline constexpr std::int32_t kFadToLbaBias = 150;
inline constexpr std::int32_t kSeekOriginLead = 4;

inline constexpr std::int32_t kReadHeadLead = 4;

[[nodiscard]] constexpr std::uint32_t seek_delay_ticks(
    std::int32_t lba_old,
    std::int32_t lba_new,
    std::uint32_t track_old,
    std::uint32_t track_new
) noexcept {
    const std::int64_t diff = static_cast<std::int64_t>(lba_new)
        - static_cast<std::int64_t>(lba_old);
    const std::int64_t mag = diff < 0 ? -diff : diff;

    std::int64_t n = diff / kSeekLbaPerTick;
    if (mag <= kSeekNearWindow) {
        n = kSeekNearTicks;
    } else if (n <= kSeekFloorTicks) {
        n = kSeekFloorTicks;
    } else if (n > kSeekCeilTicks) {
        n = kSeekCeilTicks;
    }

    if (track_old != track_new) {
        n += kSeekCrossTrackAdd;
    } else if (mag > kSeekLongSameTrack) {
        n += kSeekLongSameTrackAdd;
    }

    if (diff < -kSeekNearWindow)
        n += kSeekBackwardAdd;

    return static_cast<std::uint32_t>(n);
}

inline constexpr std::uint32_t kSeekMinTicks = 2;
inline constexpr std::uint32_t kSeekMaxTicks = 40;

static_assert(seek_delay_ticks(0, 0, 0, 0) == kSeekMinTicks);
static_assert(seek_delay_ticks(0, 4, 0, 0) == 2);
static_assert(seek_delay_ticks(0, -4, 0, 0) == 2);
static_assert(seek_delay_ticks(0, 5, 0, 0) == 4);
static_assert(seek_delay_ticks(0, -5, 0, 0) == 6);
static_assert(seek_delay_ticks(0, 10'000, 0, 0) == 5);
static_assert(seek_delay_ticks(0, 10'001, 0, 0) == 25);
static_assert(seek_delay_ticks(0, 26'000, 0, 0) == 33);
static_assert(seek_delay_ticks(0, 30'000, 0, 0) == 33);
static_assert(seek_delay_ticks(0, -30'000, 0, 0) == 26);
static_assert(seek_delay_ticks(0, 5, 0, 1) == 31);
static_assert(seek_delay_ticks(0, -30'000, 0, 1) == 33);
static_assert(seek_delay_ticks(0, 26'000, 0, 1) == kSeekMaxTicks);

}
