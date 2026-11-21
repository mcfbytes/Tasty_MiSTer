// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/thread_cpu_clock.h"

#include <ctime>

#include "os/timespec.h"

namespace mister::os {

RawMonotonicClock::time_point ThreadCpuClock::now() const {
    timespec ts{};
    ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return RawMonotonicClock::time_point{of_timespec(ts)};
}

}
