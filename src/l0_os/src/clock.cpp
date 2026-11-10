// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/raw_monotonic_clock.h"

#include <cerrno>
#include <chrono>
#include <ctime>

#include "clock_bridge.h"
#include "infra/error.h"

namespace mister::os {

RawMonotonicClock::time_point RawMonotonicClock::now() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return time_point{detail::to_ns(ts)};
}

namespace detail {

[[nodiscard]] std::chrono::nanoseconds raw_to_monotonic(RawMonotonicClock::time_point at) noexcept {
    timespec raw_ts{};
    timespec mono_ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &raw_ts);
    ::clock_gettime(CLOCK_MONOTONIC, &mono_ts);
    return at.time_since_epoch() + (to_ns(mono_ts) - to_ns(raw_ts));
}

[[nodiscard]] Ex<void> sleep_until_raw(RawMonotonicClock::time_point at) {
    const std::chrono::nanoseconds mono_deadline = raw_to_monotonic(at);

    if (mono_deadline <= std::chrono::nanoseconds::zero())
        return {};

    const timespec abs_ts = to_timespec(mono_deadline);

    int rc = 0;
    do {
        rc = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &abs_ts, nullptr);
    } while (rc == EINTR);
    if (rc != 0)
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), rc));
    return {};
}

}

}
