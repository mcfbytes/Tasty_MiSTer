// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "os/raw_monotonic_clock.h"

namespace mister::os {

using MonoInstant = std::chrono::steady_clock::time_point;

[[nodiscard]] constexpr std::chrono::nanoseconds since_boot(MonoInstant at) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(at.time_since_epoch());
}

class IClock {
public:
    virtual ~IClock() = default;

    virtual RawMonotonicClock::time_point now() const = 0;
};

}
