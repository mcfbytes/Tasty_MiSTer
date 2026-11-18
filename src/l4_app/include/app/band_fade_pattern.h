// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
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
class BandFadePattern final : public FrontEndPattern {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void paint(PxPlane& canvas, HdRect box) const noexcept override;
};

template <Axis A>
void BandFadePattern<A>::paint(PxPlane& canvas, HdRect box) const noexcept {
    TASTY_SEAT_BODY(BandFadePattern);
    constexpr std::int32_t kRamp = 7;
    constexpr std::int32_t kMaxGray = 255;
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
    const std::int32_t size = span / kRamp;
    if (size <= 0)
        return;
    std::int32_t pos = 0;
    const auto pixel_here = [&] {
        const std::int32_t base = kRamp * pos / span + 1;

        const std::int32_t first = ((base - 1) * span + kRamp - 1) / kRamp;
        const std::int32_t left = size - (pos - first);
        const std::int32_t step = left > 0 ? left : 0;
        const std::int32_t gray = kMaxGray * step / size;
        const bool red = (base & kRedBit) != 0;
        const bool green = (base & kGreenBit) != 0;
        const bool blue = (base & kBlueBit) != 0;
        return hd_pack565(rgb_from_channels(
            ColorChannel::of(red ? gray : 0),
            ColorChannel::of(green ? gray : 0),
            ColorChannel::of(blue ? gray : 0)
        ));
    };

    if constexpr (A == Axis::Horizontal) {
        for (pos = 0; pos < on.h; ++pos)
            std::fill_n(plane->row(PlaneRow{pos}).begin(), on.w, pixel_here());
    } else {
        const auto first_row = plane->row(PlaneRow{0}).begin();
        auto px = first_row;
        for (pos = 0; pos < on.w; ++pos, ++px)
            *px = pixel_here();
        for (std::int32_t y = 1; y < on.h; ++y)
            std::copy_n(first_row, on.w, plane->row(PlaneRow{y}).begin());
    }
}

}
