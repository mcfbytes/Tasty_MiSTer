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
        const float card = magnitude_cardinal(raw);
        if (card > max_cardinal)
            max_cardinal = card;
        if (diagonal) {
            const auto ax = magnitude(raw, AnalogXy::Axis::X);
            const auto ay = magnitude(raw, AnalogXy::Axis::Y);
            const auto rr = ax * ax + ay * ay;
            if (rr > max_range_sq)
                max_range_sq = rr;
        }
    }

    [[nodiscard]] StickCal cal() const noexcept {
        return StickCal{max_cardinal, std::sqrt(max_range_sq)};
    }
};

}
