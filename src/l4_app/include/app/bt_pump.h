// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/diag_log.h"
#include "infra/error.h"
#include "os/clock.h"
#include "infra/seat.h"

namespace mister::app {

class BtPump {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using Probe = Ex<bool> (*)();

    BtPump(const os::IClock& clock, Probe probe) noexcept : clock_(&clock), probe_(probe) {}

    static constexpr std::chrono::milliseconds kDelay{6000};

    void set_diag(xthread::DiagLog* d) noexcept { diag_ = d; }

    void tick();

    struct Stats {
        std::uint32_t arms = 0;
        std::uint32_t fires = 0;
        std::uint32_t suppressed_by_probe = 0;
        std::uint32_t probe_failures = 0;
        std::uint32_t spawn_failures = 0;
    };
    Stats stats() const noexcept { return stats_; }
    bool spent() const noexcept { return state_ == State::Spent; }

private:
    enum class State : std::uint8_t { Unarmed, Armed, Spent };

    const os::IClock* clock_;
    Probe probe_;
    State state_ = State::Unarmed;
    std::chrono::nanoseconds due_{};
    Stats stats_{};
    xthread::DiagLog* diag_ = nullptr;
};

}  // namespace mister::app
