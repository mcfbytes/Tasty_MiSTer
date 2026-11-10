// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/monotonic_clock.h"

#include <ctime>

#include "clock_bridge.h"

namespace mister::os {

RawMonotonicClock::time_point MonotonicClock::now() const {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return RawMonotonicClock::time_point{detail::to_ns(ts)};
}

}
