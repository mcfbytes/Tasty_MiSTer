// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::xthread {

using PauseGen = std::uint32_t;

template <KickPolicy Kick = EventFdKick>
class BasicPauseLatch {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::atomic<PauseGen>::is_always_lock_free);

public:
    explicit BasicPauseLatch(BasicWakeFlag<Kick>& seat_wake,
                             BasicWakeFlag<Kick>* asker_wake = nullptr) noexcept
        : seat_wake_(&seat_wake), asker_wake_(asker_wake) {}
    BasicPauseLatch(const BasicPauseLatch&) = delete;
    BasicPauseLatch& operator=(const BasicPauseLatch&) = delete;
    BasicPauseLatch(BasicPauseLatch&&) = delete;
    BasicPauseLatch& operator=(BasicPauseLatch&&) = delete;

    void pause(PauseGen g) noexcept {
        ask_.store(g, std::memory_order_release);
        seat_wake_->kick_if_armed();
    }
    void resume() noexcept {
        ask_.store(0, std::memory_order_release);
        seat_wake_->kick_if_armed();
    }
    [[nodiscard]] bool paused_at(PauseGen g) const noexcept {
        return paused_.load(std::memory_order_acquire) == g;
    }
    [[nodiscard]] bool running() const noexcept {
        return paused_.load(std::memory_order_acquire) == 0;
    }
    [[nodiscard]] PauseGen asked() const noexcept { return ask_.load(std::memory_order_acquire); }

    template <class Body>
    [[nodiscard]] bool observe(Body& b) noexcept {
        const PauseGen a = ask_.load(std::memory_order_acquire);
        const PauseGen p = paused_.load(std::memory_order_relaxed);
        if (a != taken_) {
            if (taken_ != 0) b.on_resume();
            if (a != 0) b.on_pause();
            taken_ = a;
        }
        if (a != p && (a == 0 || b.quiescent())) {
            paused_.store(a, std::memory_order_release);
            if (asker_wake_ != nullptr) asker_wake_->kick_if_armed();
        }
        return a != 0 && paused_.load(std::memory_order_relaxed) == a;
    }

    [[nodiscard]] bool pending() const noexcept {
        return ask_.load(std::memory_order_acquire) != paused_.load(std::memory_order_relaxed);
    }

private:
    BasicWakeFlag<Kick>* seat_wake_;
    BasicWakeFlag<Kick>* asker_wake_;
    PauseGen taken_ = 0;
    std::atomic<PauseGen> ask_{0};
    alignas(64) std::atomic<PauseGen> paused_{0};
};

using PauseLatch = BasicPauseLatch<>;

}  // namespace mister::xthread
