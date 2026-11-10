// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MISTER_APP_IDLE_BLANK_H
#define MISTER_APP_IDLE_BLANK_H

#include <chrono>
#include <cstdint>
#include <optional>
#include "infra/seat.h"
#include "os/raw_monotonic_clock.h"

namespace mister::app {

class IdleBlank {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void set_timeout_minutes(std::chrono::minutes m) noexcept;

    std::optional<bool> tick(
        os::RawMonotonicClock::time_point now,
        std::uint32_t activity,
        bool grabbed
    ) noexcept;

    bool blanked() const noexcept { return blanked_; }

    bool enabled() const noexcept { return minutes_ != std::chrono::minutes{}; }

    std::uint32_t blanks() const noexcept { return blanks_; }

    std::uint32_t wakes() const noexcept { return wakes_; }

private:
    os::RawMonotonicClock::time_point deadline_{};
    std::uint32_t last_activity_ = 0;
    std::uint32_t blanks_ = 0;
    std::uint32_t wakes_ = 0;
    std::chrono::minutes minutes_{};
    bool armed_ = false;
    bool blanked_ = false;
    bool seen_ = false;
};

}

#endif
