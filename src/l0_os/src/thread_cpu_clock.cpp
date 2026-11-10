// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/thread_cpu_clock.h"

#include <ctime>

#include "clock_bridge.h"

namespace mister::os {

RawMonotonicClock::time_point ThreadCpuClock::now() const {
    timespec ts{};
    ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return RawMonotonicClock::time_point{detail::to_ns(ts)};
}

}
