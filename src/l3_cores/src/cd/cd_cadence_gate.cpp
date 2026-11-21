// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd/cd_cadence_gate.h"

#include <chrono>
#include <cstdint>

namespace mister::cores::cd {

bool CdCadenceGate::admit(bool active, CdSpeedIndex speed_index) noexcept {
    const std::chrono::microseconds period = ticks_.period(active, speed_index);
    const std::chrono::nanoseconds period_ns = period;
    const os::RawMonotonicClock::time_point now = clock_.now();

    if (period > ticks_.caller && next_due_.has_value() && now < next_due_->at()) {
        ++gated_ticks_;
        return false;
    }

    const os::RawMonotonicClock::time_point base = next_due_ ? next_due_->at() : now;
    next_due_ = os::Deadline::at_point(base + period_ns);

    if (next_due_->at() < now)
        next_due_ = os::Deadline::at_point(now + period_ns);
    return true;
}

}
