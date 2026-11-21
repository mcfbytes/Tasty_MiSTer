// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "cores/cd/cd_tick_periods.h"
#include "infra/niche.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/deadline.h"

namespace mister::cores::cd {

class CdCadenceGate {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdCadenceGate(const CdTickPeriods& ticks, const os::IClock& c) noexcept
        : ticks_(ticks),
          clock_(c) {}

    [[nodiscard]] bool admit(bool active, CdSpeedIndex speed_index) noexcept;

    [[nodiscard]] std::uint32_t gated_ticks() const noexcept { return gated_ticks_; }

private:
    CdTickPeriods ticks_;
    const os::IClock& clock_;

    infra::Niche<os::Deadline> next_due_{};
    std::uint32_t gated_ticks_ = 0;
};

}
