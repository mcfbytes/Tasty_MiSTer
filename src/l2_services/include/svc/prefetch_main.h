// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/error.h"
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
    explicit PrefetchMain(ChdPrefetch& prefetch) noexcept : prefetch_(prefetch) {}
    PrefetchMain(const PrefetchMain&) = delete;
    PrefetchMain& operator=(const PrefetchMain&) = delete;
    PrefetchMain(PrefetchMain&&) = delete;
    PrefetchMain& operator=(PrefetchMain&&) = delete;

    [[nodiscard]] Ex<void> open() noexcept;

    [[nodiscard]] ChdPrefetch& prefetch() noexcept { return prefetch_; }

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return stopping() || prefetch_.idle(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return prefetch_.park_ms(); }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return *park_; }
    [[nodiscard]] bool paused() noexcept { return pause_.observe(*this); }
    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }
    void settle() noexcept { prefetch_.release(); }

    void on_pause() noexcept { prefetch_.park_now(); }
    void on_resume() noexcept {}
    [[nodiscard]] bool quiescent() const noexcept { return true; }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

private:
    ChdPrefetch& prefetch_;
    xthread::PauseLatch pause_{prefetch_.wake()};
    xthread::WakeFlag stop_{};
    std::optional<xthread::SeatPark> park_{};
};

static_assert(xthread::SeatBody<PrefetchMain>);

}  // namespace mister::svc
