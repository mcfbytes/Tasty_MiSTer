// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <ctime>

namespace mister::os {

[[nodiscard]] constexpr std::chrono::nanoseconds of_timespec(const timespec& ts) noexcept {
    return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

[[nodiscard]] constexpr timespec to_timespec(std::chrono::nanoseconds span) noexcept {
    const auto whole = std::chrono::duration_cast<std::chrono::duration<std::time_t>>(span);
    const auto part = std::chrono::duration_cast<std::chrono::duration<long, std::nano>>(
        span - whole
    );
    timespec ts{};
    ts.tv_sec = whole.count();
    ts.tv_nsec = part.count();
    return ts;
}

[[nodiscard]] inline std::chrono::steady_clock::time_point steady_now() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return std::chrono::steady_clock::time_point{
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(of_timespec(ts))
    };
}

}
