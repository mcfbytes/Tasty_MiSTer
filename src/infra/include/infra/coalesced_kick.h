// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "infra/error.h"
#include "infra/kick_policy.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::xthread {

template <SeatTag Producer, SeatTag Consumer, KickPolicy Kick = EventFdKick>
class BasicCoalescedKick {
    TASTY_SEAT_MEDIATOR(Any, Any);

public:
    explicit BasicCoalescedKick(typename BasicWakeFlag<Kick>::Kicker target) noexcept
        : target_(target) {}

    BasicCoalescedKick(const BasicCoalescedKick&) = delete;
    BasicCoalescedKick& operator=(const BasicCoalescedKick&) = delete;
    BasicCoalescedKick(BasicCoalescedKick&&) = delete;
    BasicCoalescedKick& operator=(BasicCoalescedKick&&) = delete;

    bool kick_once() noexcept {
        seat_assert<Producer>(ERR_SITE(), "CoalescedKick::kick_once off its producer seat");
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (armed_.exchange(true, std::memory_order_seq_cst))
            return false;
        target_.kick();
        return true;
    }

    void consume() noexcept {
        seat_assert<Consumer>(ERR_SITE(), "CoalescedKick::consume off its consumer seat");
        armed_.store(false, std::memory_order_seq_cst);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    [[nodiscard]] bool outstanding() const noexcept {
        return armed_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> armed_{false};
    typename BasicWakeFlag<Kick>::Kicker target_;
};

template <SeatTag P, SeatTag C>
using CoalescedKick = BasicCoalescedKick<P, C, EventFdKick>;

}
