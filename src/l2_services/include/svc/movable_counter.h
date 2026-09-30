// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "infra/seat.h"

namespace mister::svc {

class MovableCounter {
    TASTY_SEAT_EXEMPT(component);

public:
    MovableCounter() = default;
    MovableCounter(const MovableCounter& o) noexcept : v_(o.get()) {}
    MovableCounter(MovableCounter&& o) noexcept : v_(o.get()) {}
    MovableCounter& operator=(const MovableCounter& o) noexcept {
        set(o.get());
        return *this;
    }
    MovableCounter& operator=(MovableCounter&& o) noexcept {
        set(o.get());
        return *this;
    }
    std::uint32_t get() const noexcept { return v_.load(std::memory_order_relaxed); }
    void set(std::uint32_t n) noexcept { v_.store(n, std::memory_order_relaxed); }

    void add(std::uint32_t n) noexcept {
        v_.store(v_.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint32_t> v_{0};
};

}  // namespace mister::svc
