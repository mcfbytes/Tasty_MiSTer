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
        return gen_ != 0 && gen_ != over_ && acked_ != gen_;
    }

    [[nodiscard]] bool parked() const noexcept {
        return gen_ != 0 && gen_ != over_ && acked_ == gen_;
    }

    [[nodiscard]] std::uint32_t gen() const noexcept { return gen_; }

    [[nodiscard]] bool engaged() const noexcept { return gen_ != 0 && gen_ != over_; }

    void release() noexcept;

    void withdraw() noexcept;

private:
    struct ParkBody {
        std::uint32_t taken = 0;
        std::uint32_t gen = 0;
        bool quiet = false;
        bool edge = false;

        void on_pause() noexcept { edge = true; }

        void on_resume() noexcept { edge = true; }

        [[nodiscard]] bool quiescent() const noexcept { return quiet && !edge && taken == gen; }
    };

    xthread::PauseLatch::Observer seen_{};
    std::uint32_t gen_ = 0;
    std::uint32_t acked_ = 0;
    std::uint32_t over_ = 0;
};

}
