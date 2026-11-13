// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/front_end_pattern.h"
#include "app/hd_rect.h"
#include "app/hd_theme.h"
#include "app/px_plane.h"
#include "app/types.h"
#include "infra/seat.h"

namespace mister::app {

class SpectrumPattern final : public FrontEndPattern {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void paint(PxPlane& canvas, HdRect box) const noexcept override;
};

inline void SpectrumPattern::paint(PxPlane& canvas, HdRect box) const noexcept {
    TASTY_SEAT_BODY(SpectrumPattern);
    constexpr std::int32_t kMaxShade = 256;
    constexpr std::int32_t kPrime = 255;
    constexpr std::int32_t kHalf = 2;
    const HdRect on = box.intersect(canvas.bounds());
    if (on.is_empty())
        return;
    const std::optional<PxPlane> plane = canvas.sub(on);
    if (!plane)
        return;
    if (on.w <= 0 || on.h <= 0)
        return;
    for (std::int32_t y = 0; y < on.h; ++y) {
        auto row = plane->row(PlaneRow{y});
        const std::int32_t blue = kMaxShade * y / on.h;
        auto px = row.begin();
        for (std::int32_t x = 0; x < on.w; ++x, ++px) {
            std::int32_t green = kMaxShade * x / on.w - blue / kHalf;
            std::int32_t red = kPrime - green - blue / kHalf;
            if (red < 0)
                red = 0;
            if (green < 0)
                green = 0;
            *px = hd_pack565(rgb_from_channels(
                ColorChannel::of(red),
                ColorChannel::of(green),
                ColorChannel::of(blue)
            ));
        }
    }
}

}
