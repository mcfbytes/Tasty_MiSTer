// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "infra/error.h"
#include "infra/prelude.h"
#include "os/raw_monotonic_clock.h"

namespace mister::os::detail {

[[nodiscard]] std::chrono::nanoseconds raw_to_monotonic(RawMonotonicClock::time_point at) noexcept;

[[nodiscard]] Ex<void> sleep_until_raw(RawMonotonicClock::time_point at);

}
