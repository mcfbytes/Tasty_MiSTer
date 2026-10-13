// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "infra/counter.h"
#include "infra/seat.h"

namespace mister {

inline constexpr std::size_t kMaxServices = 24;
inline constexpr std::size_t kMaxLines = 16;
inline constexpr std::size_t kNumRings = 4;

}  // namespace mister

namespace mister::xthread {

struct alignas(64) RtStats {
    TASTY_SEAT_MEDIATOR(Any, Diag);
    xthread::Counter deadline_miss[kMaxServices];
    xthread::Counter ring_drops[kNumRings];
    xthread::Counter spin_timeouts;
    xthread::Counter spurious_causes;
    std::atomic<std::uint64_t> heartbeat;
    std::atomic<std::uint32_t> dump_requests;
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

}  // namespace mister::xthread
