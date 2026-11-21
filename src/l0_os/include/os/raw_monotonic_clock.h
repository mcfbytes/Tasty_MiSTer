// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>

#include "os/timespec.h"

namespace mister::os {

class RawMonotonicClock {
public:
    using rep = std::int64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<RawMonotonicClock, duration>;

    static constexpr bool is_steady = true;

    [[nodiscard]] static time_point now() noexcept {
        timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return time_point{of_timespec(ts)};
    }
};

[[nodiscard]] constexpr std::chrono::nanoseconds since_boot(
    RawMonotonicClock::time_point at
) noexcept {
    return at.time_since_epoch();
}

}
