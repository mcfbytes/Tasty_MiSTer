// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/floor_ratio_walk.h"
#include "app/front_end_pattern.h"
#include "app/hd_rect.h"
#include "app/hd_theme.h"
#include "app/px_plane.h"
#include "app/ratio.h"
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
    if (on.w <= 0 || on.h <= 0)
        return;

    constexpr std::int32_t kAlong = A == Axis::Horizontal ? kMaxShade : kRamp;
    constexpr std::int32_t kDown = A == Axis::Horizontal ? kRamp : kMaxShade;
    const FloorRatioWalk across = FloorRatioWalk::of(
        Ratio{.num = Ratio::Term::of(kAlong), .den = Ratio::Term::of(on.w)}
    );
    for (std::int32_t y = 0; y < on.h; ++y) {
        const std::int32_t down_q = kDown * y / on.h;
        auto row = plane->row(PlaneRow{y});
        auto px = row.begin();
        FloorRatioWalk along = across;
        for (std::int32_t x = 0; x < on.w; ++x, ++px, along.step()) {
            const std::int32_t band = A == Axis::Horizontal ? down_q
                                                            : along.quotient<std::int32_t>();
            const bool inv = (band & 1) != 0;
            const std::int32_t half = band >> 1;
            const std::int32_t base = (inv ? half : kBase - half) + 1;
            std::int32_t gray = A == Axis::Horizontal ? along.quotient<std::int32_t>() : down_q;
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
