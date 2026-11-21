// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/narrow_duration.h"
#include "infra/saturating.h"
#include "os/clock.h"
#include "os/timespec.h"

namespace mister::app {

using StatusMicros = infra::Micros32;
using LogMillis = infra::Millis32;

using LogInstant = os::MonoInstant;

[[nodiscard]] inline LogInstant log_now() noexcept {
    return os::steady_now();
}

[[nodiscard]] inline std::chrono::nanoseconds log_stamp(LogInstant at) noexcept {
    return os::since_boot(at);
}

using infra::saturated;

[[nodiscard]] constexpr std::uint32_t sat_u32(std::uint64_t v) noexcept {
    return infra::sat_add(std::uint32_t{0}, v);
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
