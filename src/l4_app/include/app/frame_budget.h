// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "app/frame_ticks.h"
#include "app/percent.h"
#include "infra/opt_ref.h"
#include "os/clock.h"

namespace mister::app {

struct FrameBudget {
    infra::OptRef<const os::IClock> clock{};
    std::chrono::nanoseconds threshold{};

    [[nodiscard]] static constexpr FrameBudget none() noexcept { return {}; }

    [[nodiscard]] static FrameBudget of(
        infra::OptRef<const os::IClock> clock,
        FrameTicks rate,
        Percent share
    ) noexcept {
        return FrameBudget{
            clock,
            std::chrono::nanoseconds{share.share_of(rate.or_60hz().period().count())}
        };
    }
};

}
