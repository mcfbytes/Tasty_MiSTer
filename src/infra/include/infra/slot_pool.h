// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "infra/counter.h"
#include "infra/indexed_array.h"
#include "infra/pool_slot.h"
#include "infra/seat.h"

namespace mister::xthread {

struct SlotPoolCounters {
    [[nodiscard]] std::uint32_t refusals() const noexcept { return refused_returns_.get(); }

protected:
    Counter refused_returns_{};
};

template <class Slot, std::size_t N, SeatTag Owner>
class SlotPool;

template <class Slot, std::size_t N, SeatTag Owner>
class SlotPool : public SlotPoolCounters {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(N > 0, "a pool with no slots can only refuse");
    static_assert(N <= 255, "the free list indexes with a uint8_t, and every slot is a PoolSlot");
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
            free_[PoolSlot::of(i)] = PoolSlot::of(i);
        free_count_ = N;
    }

    SlotPool(const SlotPool&) = delete;
    SlotPool& operator=(const SlotPool&) = delete;

    [[nodiscard]] Lease acquire() noexcept {
        on_owner_(ERR_SITE(), "SlotPool::acquire off the owner seat");
        const std::size_t n = free_count_.load(std::memory_order_relaxed);
        if (n == 0)
            return Lease{nullptr, Return{this}};
        const PoolSlot idx = free_[PoolSlot::of(n - 1)];
        free_count_.store(n - 1, std::memory_order_relaxed);
        out_[idx] = true;
        return Lease{&slots_[idx], Return{this}};
    }

    [[nodiscard]] std::optional<PoolSlot> index_of(const Slot* s) const noexcept {
        const Slot* const base = &slots_[PoolSlot::of(0)];
        if (s == nullptr || s < base || s >= base + N)
            return std::nullopt;
        return PoolSlot::of(s - base);
    }

    [[nodiscard]] Slot& at(PoolSlot idx) noexcept { return slots_[idx]; }

    [[nodiscard]] const Slot& at(PoolSlot idx) const noexcept { return slots_[idx]; }

    template <infra::ArrayIndex Index>
    [[nodiscard]] constexpr std::optional<PoolSlot> slot_of(Index index) const noexcept {
        if (static_cast<std::size_t>(index.v) >= N)
            return std::nullopt;
        return PoolSlot::of(index.v);
    }

    static constexpr std::size_t capacity() noexcept { return N; }

    static constexpr SeatTag owner_seat() noexcept { return Owner; }

    [[nodiscard]] std::uint32_t refused_returns() const noexcept { return refusals(); }

    [[nodiscard]] std::size_t available() const noexcept {
        return free_count_.load(std::memory_order_relaxed);
    }

private:
    static void on_owner_(
        [[maybe_unused]] infra::ErrorSite site,
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
        const auto idx = index_of(s);
        if (!idx)
            return;

        if (!out_[*idx]) {
            refused_returns_.add();
            return;
        }
        out_[*idx] = false;
        const std::size_t n = free_count_.load(std::memory_order_relaxed);
        free_[PoolSlot::of(n)] = *idx;
        free_count_.store(n + 1, std::memory_order_relaxed);
    }

    infra::IndexedArray<Slot, N, PoolSlot> slots_{};
    infra::IndexedArray<PoolSlot, N, PoolSlot> free_{};
    infra::IndexedArray<bool, N, PoolSlot> out_{};
    std::atomic<std::size_t> free_count_{0};
};

}
