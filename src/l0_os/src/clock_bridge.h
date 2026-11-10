// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <ctime>

#include "infra/error.h"
#include "os/raw_monotonic_clock.h"

namespace mister::os::detail {

inline std::chrono::nanoseconds to_ns(const timespec& ts) {
    return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

inline timespec to_timespec(std::chrono::nanoseconds ns) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(ns);
    timespec ts{};
    ts.tv_sec = static_cast<std::time_t>(secs.count());
    ts.tv_nsec = static_cast<long>((ns - secs).count());
    return ts;
}

[[nodiscard]] std::chrono::nanoseconds raw_to_monotonic(RawMonotonicClock::time_point at) noexcept;

[[nodiscard]] Ex<void> sleep_until_raw(RawMonotonicClock::time_point at);

}
