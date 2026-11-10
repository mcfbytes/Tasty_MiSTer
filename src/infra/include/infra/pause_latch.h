// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include "infra/observed.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "infra/pause_gen.h"

namespace mister::xthread {

template <KickPolicy Kick = EventFdKick>
class BasicPauseLatch {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::atomic<PauseGen>::is_always_lock_free);

public:
    explicit BasicPauseLatch(
        BasicWakeFlag<Kick>& seat_wake,
        infra::OptRef<BasicWakeFlag<Kick>> asker_wake = {}
    ) noexcept
        : seat_wake_(seat_wake),
          asker_wake_(asker_wake) {}

    BasicPauseLatch(Observed, infra::OptRef<BasicWakeFlag<Kick>> asker_wake) noexcept
        : asker_wake_(asker_wake) {}

    BasicPauseLatch(const BasicPauseLatch&) = delete;
    BasicPauseLatch& operator=(const BasicPauseLatch&) = delete;
    BasicPauseLatch(BasicPauseLatch&&) = delete;
    BasicPauseLatch& operator=(BasicPauseLatch&&) = delete;

    void pause(PauseGen g) noexcept {
        ask_.store(g, std::memory_order_release);
        if (seat_wake_)
            seat_wake_->kick_if_armed();
    }

    void resume() noexcept {
        ask_.store(PauseGen::none(), std::memory_order_release);
        if (seat_wake_)
            seat_wake_->kick_if_armed();
    }

    [[nodiscard]] bool paused_at(PauseGen g) const noexcept {
        return paused_.load(std::memory_order_acquire) == g;
    }

    [[nodiscard]] bool running() const noexcept {
        return paused_.load(std::memory_order_acquire).is_none();
    }

    [[nodiscard]] PauseGen asked() const noexcept { return ask_.load(std::memory_order_acquire); }

    class Observer {
        TASTY_SEAT_EXEMPT(component);
        friend class BasicPauseLatch;

    public:
        Observer() = default;
        Observer(const Observer&) = delete;
        Observer& operator=(const Observer&) = delete;

        [[nodiscard]] PauseGen taken() const noexcept { return taken_; }

    private:
        PauseGen taken_ = PauseGen::none();
    };

    template <class Body>
    [[nodiscard]] bool observe(Observer& seen, Body& b) noexcept {
        const PauseGen a = ask_.load(std::memory_order_acquire);
        const PauseGen p = paused_.load(std::memory_order_relaxed);
        if (a != seen.taken_) {
            if (!seen.taken_.is_none())
                b.on_resume();
            if (!a.is_none())
                b.on_pause();
            seen.taken_ = a;
        }
        if (a != p && (a.is_none() || b.quiescent())) {
            paused_.store(a, std::memory_order_release);
            if (asker_wake_)
                asker_wake_->kick_if_armed();
        }
        return !a.is_none() && paused_.load(std::memory_order_relaxed) == a;
    }

    [[nodiscard]] bool pending() const noexcept {
        return ask_.load(std::memory_order_acquire) != paused_.load(std::memory_order_relaxed);
    }

private:
    infra::OptRef<BasicWakeFlag<Kick>> seat_wake_;
    infra::OptRef<BasicWakeFlag<Kick>> asker_wake_;
    std::atomic<PauseGen> ask_{PauseGen::none()};
    alignas(64) std::atomic<PauseGen> paused_{PauseGen::none()};
};

using PauseLatch = BasicPauseLatch<>;

}
