// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/xthread/counter.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "os/epoll_set.h"
#include "infra/xthread/park_fds.h"
#include "infra/park_timeout.h"
#include "infra/prelude.h"
#include "infra/xthread/pause_latch.h"
#include "infra/seat.h"
#include "infra/xthread/seat_main.h"
#include "infra/xthread/seat_park.h"
#include "infra/xthread/wake_flag.h"
#include "svc/io_coworker.h"
#include "svc/io_pass.h"

namespace mister::svc {

inline constexpr infra::ParkTimeout kIoBlockForever = infra::kForever;
static_assert(kIoBlockForever < infra::kNoWait, "an idle FIFO-23 seat must not wake on a timer");

class IoMain final : public xthread::SeatMain<IoMain> {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kMaxCoworkers = 11;

    static constexpr std::uint32_t kMaxCoworkersDetail = kMaxCoworkers;

    explicit IoMain(
        xthread::ParkFds fds,
        infra::OptRef<xthread::WakeFlag> asker = {},
        infra::OptRef<IoPassCell> pass = {}
    ) noexcept;
    IoMain(
        xthread::ParkFds fds,
        infra::OptRef<xthread::WakeFlag> asker,
        const os::EpollSet& doorbells,
        infra::OptRef<IoPassCell> pass = {}
    ) noexcept;
    IoMain(const IoMain&) = delete;
    IoMain& operator=(const IoMain&) = delete;
    IoMain(IoMain&&) = delete;
    IoMain& operator=(IoMain&&) = delete;

    [[nodiscard]] Ex<void> add_coworker(
        IIoCoworker& c,
        LaneClass cls = LaneClass::Deadline
    ) noexcept;

    [[nodiscard]] xthread::WakeFlag& wake() noexcept { return wake_; }

    [[nodiscard]] const xthread::Counter& bulk_deferred() const noexcept { return bulk_deferred_; }

    void start() noexcept;

    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;

    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept {
        infra::ParkTimeout period = kIoBlockForever;
        for (std::uint8_t i = 0; i < n_coworkers_; ++i)
            period = infra::sooner_of(period, lanes_[i].lane->poll_period());
        return period;
    }

    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return pause_.observe(pause_seen_, *this); }

    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }

    void settle() noexcept;

    void on_pause() noexcept;
    void on_resume() noexcept;
    [[nodiscard]] bool quiescent() const noexcept;

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

private:
    struct Lane {
        IIoCoworker* lane;
        LaneClass cls = LaneClass::Deadline;
    };

    xthread::WakeFlag& wake_;
    xthread::PauseLatch pause_;
    xthread::PauseLatch::Observer pause_seen_{};
    xthread::WakeFlag stop_;
    Lane lanes_[kMaxCoworkers]{};
    std::uint8_t n_coworkers_ = 0;
    xthread::Counter bulk_deferred_;
    infra::OptRef<IoPassCell> pass_cell_;
    IoPass pass_acc_{};
    IoPassCount bulk_run_{};
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<IoMain>);

}
