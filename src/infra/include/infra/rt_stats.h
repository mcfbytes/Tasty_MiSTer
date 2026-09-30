// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include "infra/counter.h"
#include "infra/seat.h"

namespace mister {

class LatHist {
    TASTY_SEAT_EXEMPT(component);

public:
    void record(std::chrono::nanoseconds d) noexcept {
        const auto ns = static_cast<std::uint64_t>(d.count());
        ++count_;
        sum_ns_ += ns;
        if (ns < min_ns_ || count_ == 1) min_ns_ = ns;
        if (ns > max_ns_) max_ns_ = ns;
    }
    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t max_ns() const noexcept { return max_ns_; }

private:
    std::uint64_t count_ = 0, sum_ns_ = 0, min_ns_ = 0, max_ns_ = 0;
};

inline constexpr std::size_t kMaxServices = 24;
inline constexpr std::size_t kMaxLines = 16;
inline constexpr std::size_t kNumRings = 4;

}  // namespace mister

namespace mister::xthread {

struct alignas(64) RtStats {
    TASTY_SEAT_MEDIATOR(Any, Diag);
    LatHist service_latency[kMaxServices];
    xthread::Counter deadline_miss[kMaxServices];
    LatHist tick_jitter;
    LatHist doorbell_wake[kMaxLines];
    xthread::Counter ring_drops[kNumRings];
    xthread::Counter spin_timeouts;
    xthread::Counter spurious_causes;
    std::atomic<std::uint64_t> heartbeat;
    std::atomic<std::uint32_t> dump_requests;
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

}  // namespace mister::xthread
