// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/nanosleep_delay.h"

#include <cerrno>
#include <chrono>
#include <ctime>

#include "clock_bridge.h"
#include "infra/prelude.h"
#include "os/clock.h"
#include "os/delay.h"
#include "os/timespec.h"

namespace mister::os {

namespace {

[[nodiscard]] int sleep_until_point(MonoInstant at) noexcept {
    const timespec abs_ts = to_timespec(since_boot(at));
    int rc = 0;
    do {
        rc = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &abs_ts, nullptr);
    } while (rc == EINTR);
    return rc;
}

}

void sleep_for(std::chrono::nanoseconds d) noexcept {
    if (d <= std::chrono::nanoseconds::zero())
        return;
    timespec ts = to_timespec(d);
    while (::nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

void sleep_until(MonoInstant at) noexcept {
    (void)sleep_until_point(at);
}

void NanosleepDelay::sleep_for(std::chrono::microseconds d) {
    mister::os::sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(d));
}

void NanosleepDelay::sleep_until(RawMonotonicClock::time_point at) {
    (void)detail::sleep_until_raw(at);
}

namespace detail {

[[nodiscard]] Ex<void> sleep_until_raw(RawMonotonicClock::time_point at) {
    const std::chrono::nanoseconds mono_deadline = raw_to_monotonic(at);

    if (mono_deadline <= std::chrono::nanoseconds::zero())
        return {};
    const int rc = sleep_until_point(MonoInstant{} + mono_deadline);
    if (rc != 0)
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), rc));
    return {};
}

}

}
