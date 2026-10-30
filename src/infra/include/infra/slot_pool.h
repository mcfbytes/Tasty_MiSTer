// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "infra/counter.h"
#include "infra/seat.h"

namespace mister::xthread {

template <class Slot, std::size_t N, SeatTag Owner>
class SlotPool {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(N > 0, "a pool with no slots can only refuse");
    static_assert(N <= 255, "the free list indexes with a uint8_t, and 0xFF is kNoIndex");
    static_assert(
        Owner != SeatTag::Unbound,
        "the free list has no read-modify-write: ONE seat leases and returns, and "
        "the pool names it"
    );

public:
    struct Return {
        SlotPool* pool = nullptr;

        void operator()(Slot* s) const noexcept {
            if (pool != nullptr)
                pool->give_back_(s);
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
        on_owner_(ERR_SITE(), "SlotPool::acquire off the owner seat");
        const std::size_t n = free_count_.load(std::memory_order_relaxed);
        if (n == 0)
            return Lease{nullptr, Return{this}};
        const std::uint8_t idx = free_[n - 1];
        free_count_.store(n - 1, std::memory_order_relaxed);
        out_[idx] = true;
        return Lease{&slots_[idx], Return{this}};
    }

    static constexpr std::uint8_t kNoIndex = 0xFF;

    [[nodiscard]] std::uint8_t index_of(const Slot* s) const noexcept {
        if (s == nullptr)
            return kNoIndex;
        return static_cast<std::uint8_t>(s - &slots_[0]);
    }

    [[nodiscard]] Slot& at(std::uint8_t idx) noexcept { return slots_[idx]; }

    [[nodiscard]] const Slot& at(std::uint8_t idx) const noexcept { return slots_[idx]; }

    [[nodiscard]] std::size_t available() const noexcept {
        return free_count_.load(std::memory_order_relaxed);
    }

    static constexpr std::size_t capacity() noexcept { return N; }

    static constexpr SeatTag owner_seat() noexcept { return Owner; }

    [[nodiscard]] std::uint32_t refused_returns() const noexcept { return refused_returns_.get(); }

private:
    static void on_owner_(
        [[maybe_unused]] std::uint16_t site,
        [[maybe_unused]] const char* what
    ) noexcept {
        if constexpr (kSeatChecksEnabled) {
            const SeatTag got = current_seat();
            seat::detail::check(got == SeatTag::Unbound ? got : Owner, site, what);
        }
    }

    void give_back_(Slot* s) noexcept {
        if (s == nullptr)
            return;
        on_owner_(ERR_SITE(), "SlotPool lease returned off the owner seat");
        const std::uint8_t idx = index_of(s);

        if (!out_[idx]) {
            refused_returns_.add();
            return;
        }
        out_[idx] = false;
        const std::size_t n = free_count_.load(std::memory_order_relaxed);
        free_[n] = idx;
        free_count_.store(n + 1, std::memory_order_relaxed);
    }

    Slot slots_[N]{};
    std::uint8_t free_[N]{};
    bool out_[N]{};
    Counter refused_returns_{};
    std::atomic<std::size_t> free_count_{0};
};

}
