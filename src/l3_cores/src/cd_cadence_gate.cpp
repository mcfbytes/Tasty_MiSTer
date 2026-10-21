// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_cadence_gate.h"

#include <cstdint>

namespace mister::cores {

bool CdCadenceGate::admit(bool active, std::uint8_t speed_index) noexcept {
    std::uint32_t period_us = active ? ticks_.stream_us : ticks_.idle_us;

    if (active && ticks_.speeds > 0) {
        const std::uint8_t idx = speed_index;
        const std::uint8_t ceiling = ticks_.speeds < 4
            ? static_cast<std::uint8_t>(ticks_.speeds - 1)
            : std::uint8_t{3};
        const std::uint8_t bounded = idx < ceiling ? idx : ceiling;
        if (const std::uint32_t override_us = ticks_.speed_us[bounded]; override_us != 0) {
            period_us = override_us;
        }
    }
    const std::int64_t period_ns = static_cast<std::int64_t>(period_us) * 1000;
    const std::int64_t now = clock_.now().count();

    if (period_us > ticks_.caller_us && next_due_ns_ != 0 && now < next_due_ns_) {
        ++gated_ticks_;
        return false;
    }

    next_due_ns_ = (next_due_ns_ == 0 ? now : next_due_ns_) + period_ns;

    if (next_due_ns_ < now)
        next_due_ns_ = now + period_ns;
    return true;
}

}
