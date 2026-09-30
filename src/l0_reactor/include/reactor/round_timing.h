// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::reactor {

inline constexpr std::size_t kRoundBuckets = 20;

enum class RoundSegment : std::uint8_t { None, Steps, RowDoorbell, RowSwept, Tail, kCount };

enum class RoundLifecycle : std::uint8_t { Rung = 1, Bind = 2, Switch = 4 };
inline constexpr std::uint8_t kRoundLifecycleAll = 7;

struct RoundTiming {
    struct Cause {
        RoundSegment seg = RoundSegment::None;
        std::uint8_t index = 0;
        std::uint8_t lifecycle = 0;
        std::uint8_t pad = 0;
    };
    struct Dist {
        std::uint64_t sum_ns = 0;
        std::uint64_t max_ns = 0;
        std::uint64_t min_ns = 0;
        std::uint32_t n = 0;
        std::uint32_t over_1ms = 0;
        std::uint32_t hist[kRoundBuckets] = {};
        Cause max_cause{};
    };
    std::uint32_t epoch = 0;
    std::uint32_t steady_max_epoch = 0;
    std::uint64_t epoch_ns = 0;
    Dist steady{}, lifecycle{}, wake{};
    std::uint64_t all_steady_max_ns = 0, all_life_max_ns = 0, all_wake_max_ns = 0;
    Cause all_steady_cause{}, all_life_cause{};
    std::uint32_t all_steady_rounds = 0, all_life_rounds = 0;
    std::uint32_t all_steady_over_1ms = 0, tick_overruns = 0;
};
static_assert(std::is_trivially_copyable_v<RoundTiming>);
static_assert(std::is_nothrow_default_constructible_v<RoundTiming>);
static_assert(sizeof(RoundTiming) <= 512, "one memcpy per epoch, bounded by a constant");

using RoundTimingCell = xthread::Telemetry<RoundTiming, SeatTag::RT>;

constexpr std::size_t round_bucket(std::uint64_t ns) noexcept {
    std::size_t b = 0;
    for (std::uint64_t v = ns >> 10; v != 0 && b < kRoundBuckets - 1; v >>= 1)
        ++b;
    return b;
}
constexpr std::uint32_t round_bucket_edge_us(std::size_t b) noexcept {
    return static_cast<std::uint32_t>(((std::uint64_t{1024} << b) + 999) / 1000);
}
constexpr std::uint32_t round_us(std::uint64_t ns) noexcept {
    const std::uint64_t us = ns / 1000;
    return us > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(us);
}
constexpr std::uint32_t round_avg_us(const RoundTiming::Dist& d) noexcept {
    return d.n == 0 ? 0u : round_us(d.sum_ns / d.n);
}

constexpr std::uint32_t round_p99_us(const RoundTiming::Dist& d) noexcept {
    if (d.n == 0) return 0;
    const std::uint32_t want = d.n - d.n / 100;
    std::uint32_t cum = 0;
    for (std::size_t b = 0; b < kRoundBuckets; ++b) {
        cum += d.hist[b];
        if (cum >= want) return round_bucket_edge_us(b);
    }
    return round_bucket_edge_us(kRoundBuckets - 1);
}

}  // namespace mister::reactor
