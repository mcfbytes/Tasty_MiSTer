// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "svc/analog_reshape.h"
#include "svc/analog_xy.h"
#include "svc/axis_cal.h"
#include "svc/stick_cal.h"

namespace mister::cores::n64 {

class JoyEmu final : public svc::IAnalogReshape {
public:
    static constexpr float kMaxDiag = 69.0f;
    static constexpr float kMaxCardinal = 85.0f;
    static constexpr float kOuterDeadzone = 2.0f;
    static constexpr float kWedgeBoundary = (kMaxCardinal - kMaxDiag) / kMaxDiag;

    static constexpr float kMaxDist = 0x1.8652acp+6f;

    [[nodiscard]] svc::AnalogXy reshape(
        svc::AnalogXy in,
        const svc::AxisCal&,
        const svc::StickCal& stick
    ) const noexcept override {

        const float x_flip = in.x < 0 ? -1.0f : 1.0f;
        const float y_flip = in.y < 0 ? -1.0f : 1.0f;
        const float abs_x = static_cast<float>(in.x) * x_flip;
        const float abs_y = static_cast<float>(in.y) * y_flip;

        float max_cardinal = stick.max_cardinal;
        float max_range = stick.max_range;
        if (max_cardinal < kMaxCardinal)
            max_cardinal = kMaxCardinal;
        if (max_range < kMaxDist)
            max_range = kMaxDist;

        const float scale_cardinal = kMaxCardinal / (max_cardinal - kOuterDeadzone);
        const float scale_range = kMaxDist / (max_range - kOuterDeadzone);
        const float scale = scale_cardinal > scale_range ? scale_cardinal : scale_range;
        const float scaled_x = abs_x * scale;
        const float scaled_y = abs_y * scale;

        const bool x_dominant = abs_x > abs_y;
        float scaled_max = x_dominant ? scaled_x : scaled_y;
        float scaled_min = x_dominant ? scaled_y : scaled_x;

        const float boundary = scaled_max + scaled_min * kWedgeBoundary;
        if (boundary > kMaxCardinal) {
            scaled_min = kMaxCardinal * scaled_min / boundary;
            scaled_max = kMaxCardinal - scaled_min * kWedgeBoundary;
        }

        const float out_max = std::nearbyintf(scaled_max * (x_dominant ? x_flip : y_flip));
        const float out_min = std::nearbyintf(scaled_min * (x_dominant ? y_flip : x_flip));
        return x_dominant
            ? svc::AnalogXy{static_cast<std::int32_t>(out_max), static_cast<std::int32_t>(out_min)}
            : svc::AnalogXy{static_cast<std::int32_t>(out_min), static_cast<std::int32_t>(out_max)};
    }
};

inline constexpr JoyEmu kJoyEmu{};

}
