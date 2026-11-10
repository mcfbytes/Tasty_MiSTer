// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "svc/filter_store.h"
#include "svc/types.h"

namespace mister::svc {

struct ScalingPolicy {
    std::uint16_t scrw = 0;
    std::uint16_t scrh = 0;
    VscaleMode vscale_mode = VscaleMode::Fit;
    std::uint8_t vscale_border = 0;
    ScalerSlotMode filter_mode{};
    bool front_end = false;

    [[nodiscard]] constexpr bool steps_height() const noexcept {
        return vscale_mode != VscaleMode::Fit;
    }

    [[nodiscard]] constexpr bool scales_by_resolution() const noexcept {
        return vscale_mode >= VscaleMode::ResolutionCoreAspect;
    }
};

}
