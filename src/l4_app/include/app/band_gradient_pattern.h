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

template <Axis A>
class BandGradientPattern final : public FrontEndPattern {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void paint(PxPlane& canvas, HdRect box) const noexcept override;
};

template <Axis A>
void BandGradientPattern<A>::paint(PxPlane& canvas, HdRect box) const noexcept {
    TASTY_SEAT_BODY(BandGradientPattern);
    constexpr std::int32_t kRamp = 14;
    constexpr std::int32_t kMaxShade = 256;
    constexpr std::int32_t kInvert = 255;
    constexpr std::int32_t kBase = 6;
    constexpr std::int32_t kRedBit = 1;
    constexpr std::int32_t kGreenBit = 2;
    constexpr std::int32_t kBlueBit = 4;
    const HdRect on = box.intersect(canvas.bounds());
    if (on.is_empty())
        return;
    const std::optional<PxPlane> plane = canvas.sub(on);
    if (!plane)
        return;
    const std::int32_t span = A == Axis::Horizontal ? on.h : on.w;
    const std::int32_t shade_span = A == Axis::Horizontal ? on.w : on.h;
    if (span <= 0 || shade_span <= 0)
        return;
    for (std::int32_t y = 0; y < on.h; ++y) {
        auto row = plane->row(PlaneRow{y});
        auto px = row.begin();
        for (std::int32_t x = 0; x < on.w; ++x, ++px) {
            const std::int32_t pos = A == Axis::Horizontal ? y : x;
            const std::int32_t shade_at = A == Axis::Horizontal ? x : y;
            const std::int32_t band = kRamp * pos / span;
            const bool inv = (band & 1) != 0;
            const std::int32_t half = band >> 1;
            const std::int32_t base = (inv ? half : kBase - half) + 1;
            std::int32_t gray = kMaxShade * shade_at / shade_span;
            if (inv)
                gray = kInvert - gray;
            const bool red = (base & kRedBit) != 0;
            const bool green = (base & kGreenBit) != 0;
            const bool blue = (base & kBlueBit) != 0;
            *px = hd_pack565(rgb_from_channels(
                ColorChannel::of(red ? gray : 0),
                ColorChannel::of(green ? gray : 0),
                ColorChannel::of(blue ? gray : 0)
            ));
        }
    }
}

}
