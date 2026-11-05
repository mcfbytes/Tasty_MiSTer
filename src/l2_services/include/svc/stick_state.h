// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>

#include "infra/seat.h"
#include "svc/analog_xy.h"
#include "svc/stick_cal.h"
#include "svc/stick_index.h"

namespace mister::svc {

struct StickState {
    TASTY_SEAT_EXEMPT(component);
    AnalogXy raw{};
    AnalogXy published{};
    float max_cardinal = 0.0f;
    float max_range_sq = 0.0f;

    void learn(bool diagonal) noexcept {
        const std::int32_t ax = raw.x < 0 ? -raw.x : raw.x;
        const std::int32_t ay = raw.y < 0 ? -raw.y : raw.y;
        const auto card = static_cast<float>(ax > ay ? ax : ay);
        if (card > max_cardinal)
            max_cardinal = card;
        if (diagonal) {
            const auto rr = static_cast<float>(ax) * static_cast<float>(ax)
                + static_cast<float>(ay) * static_cast<float>(ay);
            if (rr > max_range_sq)
                max_range_sq = rr;
        }
    }

    [[nodiscard]] StickCal cal() const noexcept {
        return StickCal{max_cardinal, std::sqrt(max_range_sq)};
    }
};

}
