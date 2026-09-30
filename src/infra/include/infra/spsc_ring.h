// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include "infra/seat.h"

namespace mister::xthread {

template <class I>
concept RingIndex =
    std::unsigned_integral<I> && !std::same_as<I, bool> && sizeof(I) <= sizeof(std::uint32_t);

template <class T, std::size_t N, class Index = std::uint32_t>
class SpscRing {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::is_trivially_copyable_v<T>,
                  "a ring payload carries NO ownership: a pointer here would run "
                  "a destructor on whichever seat popped it. Most are small PODs "
                  "keyed into pre-sized tables; a payload whose table cannot "
                  "outlive the wake that reads it rides BY VALUE instead, and "
                  "prices its own footprint. see notes: spsc_ring.notes.md");
    static_assert((N & (N - 1)) == 0, "power-of-two capacity");
    static_assert(RingIndex<Index>,
                  "the index is an unsigned word no wider than its storage: wrap is "
                  "defined, and the 32-bit atomic holds it. see notes: spsc_ring.notes.md");
    static_assert(N <= std::numeric_limits<Index>::max(),
                  "occupancy must be representable at this index width");

public:
    using index_type = Index;

    SpscRing() = default;

    explicit SpscRing(Index at) noexcept : head_(at), tail_(at) {}

    bool push(const T& v) noexcept {
        const Index head = static_cast<Index>(head_.load(std::memory_order_relaxed));
        const Index tail = static_cast<Index>(tail_.load(std::memory_order_acquire));
        if (static_cast<Index>(head - tail) >= N) return false;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
        slots_[head & (N - 1)] = v;
#pragma GCC diagnostic pop
        const std::uint32_t next = static_cast<Index>(head + 1);
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept {
        const Index tail = static_cast<Index>(tail_.load(std::memory_order_relaxed));
        const Index head = static_cast<Index>(head_.load(std::memory_order_acquire));
        if (head == tail) return std::nullopt;
        T v = slots_[tail & (N - 1)];
        const std::uint32_t next = static_cast<Index>(tail + 1);
        tail_.store(next, std::memory_order_release);
        return v;
    }

    std::size_t size() const noexcept { return static_cast<Index>(pushed() - popped()); }

    Index pushed() const noexcept {
        return static_cast<Index>(head_.load(std::memory_order_acquire));
    }
    Index popped() const noexcept {
        return static_cast<Index>(tail_.load(std::memory_order_acquire));
    }

private:
    alignas(64) std::atomic<std::uint32_t> head_{0};
    alignas(64) std::atomic<std::uint32_t> tail_{0};
    alignas(64) T slots_[N];
};

}  // namespace mister::xthread
