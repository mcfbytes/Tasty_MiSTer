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

class CheckersPattern final : public FrontEndPattern {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void paint(PxPlane& canvas, HdRect box) const noexcept override;
};

inline void CheckersPattern::paint(PxPlane& canvas, HdRect box) const noexcept {
    TASTY_SEAT_BODY(CheckersPattern);
    constexpr Rgb888 kColour1{0x888888u};
    constexpr Rgb888 kColour2{0x666666u};
    constexpr std::int32_t kDivisions = 128;
    const HdRect on = box.intersect(canvas.bounds());
    if (on.is_empty())
        return;
    const std::optional<PxPlane> plane = canvas.sub(on);
    if (!plane)
        return;
    const std::int32_t size = on.w / kDivisions;
    if (size <= 0)
        return;
    for (std::int32_t y = 0; y < on.h; ++y) {
        auto row = plane->row(PlaneRow{y});
        const std::int32_t c1 = (y / size) & 1;
        auto px = row.begin();
        for (std::int32_t x = 0; x < on.w; ++x, ++px) {
            const bool c2 = (c1 ^ ((x / size) & 1)) != 0;
            *px = hd_pack565(c2 ? kColour2 : kColour1);
        }
    }
}

}
