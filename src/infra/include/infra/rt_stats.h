// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "infra/counter.h"
#include "infra/heartbeat.h"
#include "infra/indexed_array.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/service_slot.h"

namespace mister {

inline constexpr std::size_t kMaxLines = 16;

}

namespace mister::xthread {

struct alignas(64) RtStats {
    TASTY_SEAT_MEDIATOR(Any, Diag);

    enum class Ring : std::uint8_t { cmd, event, prefetch, log, kCount };

    infra::IndexedArray<xthread::Counter, kMaxServices, ServiceSlot> deadline_miss{};
    xthread::Counter spin_timeouts;
    xthread::Counter spurious_causes;
    std::atomic<std::uint64_t> heartbeat;
    std::atomic<std::uint32_t> dump_requests;

    [[nodiscard]] xthread::Counter& drops(Ring ring) noexcept {
        return ring_drops[infra::ordinal(ring)];
    }

    [[nodiscard]] const xthread::Counter& drops(Ring ring) const noexcept {
        return ring_drops[infra::ordinal(ring)];
    }

private:
    std::array<xthread::Counter, infra::ordinal(Ring::kCount)> ring_drops;
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

}
