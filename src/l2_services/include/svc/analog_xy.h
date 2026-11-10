// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct AnalogXy {

    enum class Axis : std::uint8_t { X, Y };
    std::int32_t x = 0;
    std::int32_t y = 0;

    static constexpr std::int32_t kAxisMax = 127;
    friend constexpr bool operator==(AnalogXy, AnalogXy) = default;
};

[[nodiscard]] constexpr AnalogXy::Axis dominant_axis(AnalogXy in) noexcept {
    return (in.x > in.y) == (in.x > -in.y) ? AnalogXy::Axis::X : AnalogXy::Axis::Y;
}

[[nodiscard]] constexpr AnalogXy::Axis other_axis(AnalogXy::Axis axis) noexcept {
    return axis == AnalogXy::Axis::X ? AnalogXy::Axis::Y : AnalogXy::Axis::X;
}

[[nodiscard]] constexpr float component(AnalogXy in, AnalogXy::Axis axis) noexcept {
    return static_cast<float>(axis == AnalogXy::Axis::X ? in.x : in.y);
}

[[nodiscard]] constexpr float magnitude(AnalogXy in, AnalogXy::Axis axis) noexcept {
    const float v = component(in, axis);
    return v > -v ? v : -v;
}

[[nodiscard]] constexpr float magnitude_cardinal(AnalogXy in) noexcept {
    return magnitude(in, dominant_axis(in));
}

}
