// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "infra/seat.h"

namespace mister::xthread {

template <class Slot, std::size_t N>
class SlotPool {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(N > 0, "a pool with no slots can only refuse");
    static_assert(N <= 255, "the free list indexes with a uint8_t, and 0xFF is kNoIndex");

public:
    struct Return {
        SlotPool* pool = nullptr;
        void operator()(Slot* s) const noexcept {
            if (pool != nullptr) pool->give_back_(s);
        }
    };

    using Lease = std::unique_ptr<Slot, Return>;

    SlotPool() noexcept {
        for (std::size_t i = 0; i < N; ++i)
            free_[i] = static_cast<std::uint8_t>(i);
        free_count_ = N;
    }
    SlotPool(const SlotPool&) = delete;
    SlotPool& operator=(const SlotPool&) = delete;

    [[nodiscard]] Lease acquire() noexcept {
        const std::size_t n = free_count_.load(std::memory_order_relaxed);
        if (n == 0) return Lease{nullptr, Return{this}};
        const std::uint8_t idx = free_[n - 1];
        free_count_.store(n - 1, std::memory_order_relaxed);
        return Lease{&slots_[idx], Return{this}};
    }

    static constexpr std::uint8_t kNoIndex = 0xFF;
    [[nodiscard]] std::uint8_t index_of(const Slot* s) const noexcept {
        if (s == nullptr) return kNoIndex;
        return static_cast<std::uint8_t>(s - &slots_[0]);
    }
    [[nodiscard]] Slot& at(std::uint8_t idx) noexcept { return slots_[idx]; }
    [[nodiscard]] const Slot& at(std::uint8_t idx) const noexcept { return slots_[idx]; }

    [[nodiscard]] std::size_t available() const noexcept {
        return free_count_.load(std::memory_order_relaxed);
    }
    static constexpr std::size_t capacity() noexcept { return N; }

private:
    void give_back_(Slot* s) noexcept {
        const std::size_t n = free_count_.load(std::memory_order_relaxed);
        if (s == nullptr || n >= N) return;
        free_[n] = index_of(s);
        free_count_.store(n + 1, std::memory_order_relaxed);
    }

    Slot slots_[N]{};
    std::uint8_t free_[N]{};
    std::atomic<std::size_t> free_count_{0};
};

}  // namespace mister::xthread
