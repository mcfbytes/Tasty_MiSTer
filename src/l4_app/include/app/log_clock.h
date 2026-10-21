// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>

#include <cstdint>

namespace mister::app {

[[nodiscard]] inline std::uint64_t log_now_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull
        + static_cast<std::uint64_t>(ts.tv_nsec);
}

[[nodiscard]] inline std::uint32_t sat_u32(std::uint64_t v) noexcept {
    return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(v);
}

}
