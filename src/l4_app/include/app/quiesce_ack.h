// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/switch_park.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"

namespace mister::app {

class QuiesceAck {
    TASTY_SEAT_RESIDENT(RT);

public:
    void poll(SwitchPark& sp) noexcept;

    void step(SwitchPark& sp, bool held_back) noexcept;

    [[nodiscard]] bool asked() const noexcept {
        return !gen_.is_none() && gen_ != over_ && acked_ != gen_;
    }

    [[nodiscard]] bool parked() const noexcept {
        return !gen_.is_none() && gen_ != over_ && acked_ == gen_;
    }

    [[nodiscard]] xthread::PauseGen gen() const noexcept { return gen_; }

    [[nodiscard]] bool engaged() const noexcept { return !gen_.is_none() && gen_ != over_; }

    void release() noexcept;

    void withdraw() noexcept;

private:
    struct ParkBody {
        xthread::PauseGen taken{};
        xthread::PauseGen gen{};
        bool quiet = false;
        bool edge = false;

        void on_pause() noexcept { edge = true; }

        void on_resume() noexcept { edge = true; }

        [[nodiscard]] bool quiescent() const noexcept { return quiet && !edge && taken == gen; }
    };

    xthread::PauseLatch::Observer seen_{};
    xthread::PauseGen gen_{};
    xthread::PauseGen acked_{};
    xthread::PauseGen over_{};
};

}
