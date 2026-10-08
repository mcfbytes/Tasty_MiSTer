// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"
#include "svc/io_coworker.h"

namespace mister::svc {

inline constexpr int kIoBlockForeverMs = -1;
static_assert(kIoBlockForeverMs < 0, "an idle FIFO-23 seat must not wake on a timer");

class IoMain final : public xthread::SeatMain<IoMain> {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kMaxCoworkers = 5;

    explicit IoMain(xthread::ParkFds fds, infra::OptRef<xthread::WakeFlag> asker = {}) noexcept;
    IoMain(const IoMain&) = delete;
    IoMain& operator=(const IoMain&) = delete;
    IoMain(IoMain&&) = delete;
    IoMain& operator=(IoMain&&) = delete;

    [[nodiscard]] bool add_coworker(IIoCoworker* c) noexcept;
    void remove_coworker(IIoCoworker* c) noexcept;

    [[nodiscard]] xthread::WakeFlag& wake() noexcept { return wake_; }

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return kIoBlockForeverMs; }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }
    [[nodiscard]] bool paused() noexcept { return pause_.observe(*this); }
    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }
    void settle() noexcept {}

    void on_pause() noexcept;
    void on_resume() noexcept;
    [[nodiscard]] bool quiescent() const noexcept { return idle(); }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

private:
    xthread::WakeFlag& wake_;
    xthread::PauseLatch pause_;
    xthread::WakeFlag stop_;
    IIoCoworker* coworkers_[kMaxCoworkers]{};
    std::uint8_t n_coworkers_ = 0;
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<IoMain>);

}  // namespace mister::svc
