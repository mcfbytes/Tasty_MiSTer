// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/clock.h"

#include "os/deadline.h"
#include "os/monotonic_clock.h"
#include "os/nanosleep_delay.h"
#include "os/thread_cpu_clock.h"

#include <cerrno>
#include <ctime>

namespace mister::os {

namespace {
std::chrono::nanoseconds to_ns(const timespec& ts) {
    return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

timespec to_timespec(std::chrono::nanoseconds ns) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(ns);
    timespec ts{};
    ts.tv_sec = static_cast<std::time_t>(secs.count());
    ts.tv_nsec = static_cast<long>((ns - secs).count());
    return ts;
}
}  // namespace

std::chrono::nanoseconds MonotonicClock::now() const {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return to_ns(ts);
}

std::chrono::nanoseconds ThreadCpuClock::now() const {
    timespec ts{};
    ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return to_ns(ts);
}

void NanosleepDelay::sleep_for(std::chrono::microseconds d) {
    if (d <= std::chrono::microseconds::zero()) return;
    timespec ts = to_timespec(std::chrono::duration_cast<std::chrono::nanoseconds>(d));
    while (::nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

namespace {

int sleep_until_raw(std::chrono::nanoseconds at) {
    timespec raw_ts{};
    timespec mono_ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &raw_ts);
    ::clock_gettime(CLOCK_MONOTONIC, &mono_ts);
    const auto mono_deadline = at + (to_ns(mono_ts) - to_ns(raw_ts));

    if (mono_deadline <= std::chrono::nanoseconds::zero()) return 0;

    const timespec abs_ts = to_timespec(mono_deadline);

    int rc = 0;
    do {
        rc = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &abs_ts, nullptr);
    } while (rc == EINTR);
    return rc;
}
}  // namespace

void NanosleepDelay::sleep_until(std::chrono::nanoseconds at) { (void)sleep_until_raw(at); }

Ex<void> Deadline::sleep_until() const {
    if (const int rc = sleep_until_raw(at_); rc != 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(rc)});
    }
    return {};
}

}  // namespace mister::os
