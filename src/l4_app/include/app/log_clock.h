// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>

namespace mister::app {

using StatusMicros = std::chrono::duration<std::uint32_t, std::micro>;
using LogMillis = std::chrono::duration<std::uint32_t, std::milli>;

using LogInstant = std::chrono::steady_clock::time_point;

[[nodiscard]] inline LogInstant log_now() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return LogInstant{std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec}};
}

[[nodiscard]] inline std::chrono::nanoseconds log_stamp(LogInstant at) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(at.time_since_epoch());
}

[[nodiscard]] constexpr std::uint32_t sat_u32(std::uint64_t v) noexcept {
    return v > std::numeric_limits<std::uint32_t>::max() ? std::numeric_limits<std::uint32_t>::max()
                                                         : static_cast<std::uint32_t>(v);
}

template <class To>
[[nodiscard]] constexpr To saturated(std::chrono::nanoseconds span) noexcept {
    if (span <= std::chrono::nanoseconds::zero())
        return To::zero();
    using Wide = std::chrono::duration<std::uint64_t, typename To::period>;
    return To{sat_u32(std::chrono::duration_cast<Wide>(span).count())};
}

[[nodiscard]] constexpr StatusMicros us_of(std::chrono::nanoseconds span) noexcept {
    return saturated<StatusMicros>(span);
}

[[nodiscard]] constexpr LogMillis ms_of(std::chrono::nanoseconds span) noexcept {
    return saturated<LogMillis>(span);
}

[[nodiscard]] constexpr StatusMicros us_between(LogInstant from, LogInstant to) noexcept {
    if (to <= from)
        return StatusMicros::zero();
    using WideMicros = std::chrono::duration<std::uint64_t, std::micro>;
    return StatusMicros{sat_u32(std::chrono::duration_cast<WideMicros>(to - from).count())};
}

}
