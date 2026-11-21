// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/raw_monotonic_clock.h"

#include <chrono>
#include <ctime>

#include "clock_bridge.h"
#include "os/timespec.h"

namespace mister::os {

namespace detail {

[[nodiscard]] std::chrono::nanoseconds raw_to_monotonic(RawMonotonicClock::time_point at) noexcept {
    timespec raw_ts{};
    timespec mono_ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &raw_ts);
    ::clock_gettime(CLOCK_MONOTONIC, &mono_ts);
    return since_boot(at) + (of_timespec(mono_ts) - of_timespec(raw_ts));
}

}

}
