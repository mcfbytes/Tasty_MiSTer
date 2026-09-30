// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/input_decode.h"
#include "app/input_park.h"
#include "infra/error.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/wake_flag.h"

namespace mister::app {

class InputMain final : public xthread::SeatMain<InputMain> {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr int kInputPollMs = 50;

    explicit InputMain(InputDecode& decode) noexcept
        : decode_(decode), park_(decode, pause_wake_) {}
    InputMain(const InputMain&) = delete;
    InputMain& operator=(const InputMain&) = delete;
    InputMain(InputMain&&) = delete;
    InputMain& operator=(InputMain&&) = delete;

    [[nodiscard]] Ex<void> open() noexcept;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return true; }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return kInputPollMs; }
    [[nodiscard]] InputPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept {
        (void)pause_.observe(*this);
        return false;
    }
    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }
    void settle() noexcept {}

    void on_pause() noexcept;
    void on_resume() noexcept { decode_.mute(false); }
    [[nodiscard]] bool quiescent() const noexcept { return true; }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

    [[nodiscard]] std::uint32_t paused_link_drops() const noexcept { return paused_link_drops_; }

private:
    InputDecode& decode_;
    xthread::WakeFlag stop_{};
    xthread::WakeFlag pause_wake_{};
    xthread::PauseLatch pause_{pause_wake_};
    std::uint32_t paused_link_drops_ = 0;
    InputPark park_;
    bool opened_ = false;
};

static_assert(xthread::SeatBody<InputMain>);

}  // namespace mister::app
