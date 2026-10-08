// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"
#include "svc/chd_prefetch.h"

namespace mister::svc {

class PrefetchMain final : public xthread::SeatMain<PrefetchMain> {
    TASTY_SEAT_RESIDENT(Prefetch);

public:
    PrefetchMain(ChdPrefetch& prefetch, xthread::ParkFds fds,
                 infra::OptRef<xthread::WakeFlag> asker = {}) noexcept
        : prefetch_(prefetch), pause_(fds.wake(), asker ? &*asker : nullptr),
          stop_(std::move(fds).take_stop()), park_(prefetch_.wake(), stop_) {}
    PrefetchMain(const PrefetchMain&) = delete;
    PrefetchMain& operator=(const PrefetchMain&) = delete;
    PrefetchMain(PrefetchMain&&) = delete;
    PrefetchMain& operator=(PrefetchMain&&) = delete;

    [[nodiscard]] ChdPrefetch& prefetch() noexcept { return prefetch_; }

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return stopping() || prefetch_.idle(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return prefetch_.park_ms(); }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }
    [[nodiscard]] bool paused() noexcept { return pause_.observe(*this); }
    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }
    void settle() noexcept { prefetch_.release(); }

    void on_pause() noexcept { prefetch_.park_now(); }
    void on_resume() noexcept {}
    [[nodiscard]] bool quiescent() const noexcept { return true; }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

private:
    ChdPrefetch& prefetch_;
    xthread::PauseLatch pause_;
    xthread::WakeFlag stop_;
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<PrefetchMain>);

}  // namespace mister::svc
