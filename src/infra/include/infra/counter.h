// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "infra/seat.h"

namespace mister::xthread {

class Counter {
    TASTY_SEAT_MEDIATOR(Any, Any);

public:
    Counter() = default;
    Counter(const Counter& o) noexcept : v_(o.get()) {}
    Counter(Counter&& o) noexcept : v_(o.get()) {}
    Counter& operator=(const Counter& o) noexcept {
        set(o.get());
        return *this;
    }
    Counter& operator=(Counter&& o) noexcept {
        set(o.get());
        return *this;
    }
    ~Counter() = default;

    void add(std::uint32_t by = 1) noexcept {
        v_.store(v_.load(std::memory_order_relaxed) + by, std::memory_order_relaxed);
    }
    void set(std::uint32_t n) noexcept { v_.store(n, std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t get() const noexcept { return v_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> v_{0};
};

}  // namespace mister::xthread
