// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/nanosleep_delay.h"

#include <cerrno>
#include <ctime>

#include "clock_bridge.h"

namespace mister::os {

void NanosleepDelay::sleep_for(std::chrono::microseconds d) {
    if (d <= std::chrono::microseconds::zero())
        return;
    timespec ts = detail::to_timespec(std::chrono::duration_cast<std::chrono::nanoseconds>(d));
    while (::nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

void NanosleepDelay::sleep_until(RawMonotonicClock::time_point at) {
    (void)detail::sleep_until_raw(at);
}

}
