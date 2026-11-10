// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "reactor/round_lifecycle.h"
#include "reactor/service_slot.h"

namespace mister::reactor {

inline constexpr std::size_t kRoundBuckets = 20;

enum class RoundSegment : std::uint8_t { None, Steps, RowDoorbell, RowSwept, Tail, kCount };

struct RoundTiming {
    struct Blame {
        RoundSegment seg = RoundSegment::None;
        ServiceSlot index{};
        RoundLifecycleSet lifecycle_marks{};
        std::uint8_t pad = 0;
    };

    struct Dist {
        std::uint64_t sum_ns = 0;
        std::uint64_t max_ns = 0;
        std::uint64_t min_ns = 0;
        std::uint32_t n = 0;
        std::uint32_t over_1ms = 0;
        std::uint32_t hist[kRoundBuckets] = {};
        Blame max_blame{};
    };

    std::uint32_t epoch = 0;
    std::uint32_t steady_max_epoch = 0;
    std::uint64_t epoch_ns = 0;
    Dist steady{}, lifecycle{}, wake{};
    std::uint64_t all_steady_max_ns = 0, all_life_max_ns = 0, all_wake_max_ns = 0;
    Blame all_steady_blame{}, all_life_blame{};
    std::uint32_t all_steady_rounds = 0, all_life_rounds = 0;
    std::uint32_t all_steady_over_1ms = 0, tick_overruns = 0;
    std::uint64_t all_steady_sum_ns = 0;
    std::uint32_t all_steady_hist[kRoundBuckets] = {};
};

static_assert(std::is_trivially_copyable_v<RoundTiming>);
static_assert(std::is_nothrow_default_constructible_v<RoundTiming>);
static_assert(sizeof(RoundTiming) == 512, "one memcpy per epoch; a new field must make room");

using RoundTimingCell = xthread::Telemetry<RoundTiming, SeatTag::RT>;

inline constexpr std::uint64_t kFirstBucketNs = 1024;
inline constexpr std::uint64_t kNsPerUs = std::chrono::nanoseconds{std::chrono::microseconds{1}}
                                              .count();
inline constexpr std::uint32_t kPercentileDenominator = 100;

constexpr std::size_t round_bucket(std::uint64_t ns) noexcept {
    std::size_t b = 0;
    for (std::uint64_t v = ns / kFirstBucketNs; v != 0 && b < kRoundBuckets - 1; v >>= 1)
        ++b;
    return b;
}

constexpr std::uint32_t round_bucket_edge_us(std::size_t b) noexcept {
    return static_cast<std::uint32_t>(((kFirstBucketNs << b) + (kNsPerUs - 1)) / kNsPerUs);
}

constexpr std::uint32_t round_us(std::uint64_t ns) noexcept {
    const std::uint64_t us = ns / kNsPerUs;
    return us > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(us);
}

constexpr std::uint32_t round_avg_us(const RoundTiming::Dist& d) noexcept {
    return d.n == 0 ? 0u : round_us(d.sum_ns / d.n);
}

constexpr std::uint32_t round_all_steady_avg_us(const RoundTiming& t) noexcept {
    return t.all_steady_rounds == 0 ? 0u : round_us(t.all_steady_sum_ns / t.all_steady_rounds);
}

constexpr std::uint32_t round_p99_us(const RoundTiming::Dist& d) noexcept {
    if (d.n == 0)
        return 0;
    const std::uint32_t want = d.n - d.n / kPercentileDenominator;
    std::uint32_t cum = 0;
    for (std::size_t b = 0; b < kRoundBuckets; ++b) {
        cum += d.hist[b];
        if (cum >= want)
            return round_bucket_edge_us(b);
    }
    return round_bucket_edge_us(kRoundBuckets - 1);
}

}
