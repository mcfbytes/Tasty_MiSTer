// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_cadence_gate.h"

#include <cstdint>

namespace mister::cores {

bool CdCadenceGate::admit(bool active, std::uint8_t speed_index) noexcept {
    std::chrono::microseconds period = active ? ticks_.stream : ticks_.idle;

    if (active && ticks_.speeds > 0) {
        const std::uint8_t idx = speed_index;
        const std::uint8_t ceiling = ticks_.speeds < 4
            ? static_cast<std::uint8_t>(ticks_.speeds - 1)
            : std::uint8_t{3};
        const std::uint8_t bounded = idx < ceiling ? idx : ceiling;
        if (const std::chrono::microseconds override_period = ticks_.speed[bounded];
            override_period != std::chrono::microseconds::zero()) {
            period = override_period;
        }
    }
    const std::int64_t period_ns = std::chrono::nanoseconds{period}.count();
    const std::int64_t now = clock_.now().count();

    if (period > ticks_.caller && next_due_ns_ != 0 && now < next_due_ns_) {
        ++gated_ticks_;
        return false;
    }

    next_due_ns_ = (next_due_ns_ == 0 ? now : next_due_ns_) + period_ns;

    if (next_due_ns_ < now)
        next_due_ns_ = now + period_ns;
    return true;
}

}
