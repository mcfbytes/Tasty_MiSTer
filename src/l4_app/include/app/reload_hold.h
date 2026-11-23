// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>

#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

class ReloadHold {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Instant = os::RawMonotonicClock::time_point;

    [[nodiscard]] bool sample(bool down, std::chrono::milliseconds hold, Instant now) noexcept {
        if (!down) {
            since_.reset();
            fired_ = false;
            return false;
        }
        if (!since_) {
            since_ = now;
            return false;
        }
        if (fired_ || now - *since_ < hold)
            return false;
        fired_ = true;
        return true;
    }

private:
    std::optional<Instant> since_{};
    bool fired_ = false;
};

}
