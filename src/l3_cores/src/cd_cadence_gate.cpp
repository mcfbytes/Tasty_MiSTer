// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_cadence_gate.h"

#include <chrono>
#include <cstdint>

namespace mister::cores {

bool CdCadenceGate::admit(bool active, CdSpeedIndex speed_index) noexcept {
    const std::chrono::microseconds period = ticks_.period(active, speed_index);
    const std::chrono::nanoseconds period_ns = period;
    const os::RawMonotonicClock::time_point now = clock_.now();

    if (period > ticks_.caller && next_due_ && now < *next_due_) {
        ++gated_ticks_;
        return false;
    }

    next_due_ = next_due_.value_or(now) + period_ns;

    if (*next_due_ < now)
        *next_due_ = now + period_ns;
    return true;
}

}
