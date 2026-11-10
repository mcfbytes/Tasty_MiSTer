// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "infra/error.h"
#include "os/clock.h"
#include "infra/seat.h"

namespace mister::os {

class Deadline {

public:
    static Deadline in(const IClock& c, std::chrono::nanoseconds d) {
        return Deadline{c.now() + d};
    }

    static Deadline immediate() { return Deadline{RawMonotonicClock::time_point{}}; }

    bool expired(const IClock& c) const { return c.now() >= at_; }

    RawMonotonicClock::time_point at() const noexcept { return at_; }

    std::chrono::nanoseconds remaining(RawMonotonicClock::time_point now) const noexcept {
        return at_ - now;
    }

    [[nodiscard]] Ex<void> sleep_until() const;

private:
    explicit Deadline(RawMonotonicClock::time_point at) : at_(at) {}

    RawMonotonicClock::time_point at_;
};

}
