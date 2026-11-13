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

class BlackPattern final : public FrontEndPattern {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void paint(PxPlane& canvas, HdRect box) const noexcept override;
};

inline void BlackPattern::paint(PxPlane& canvas, HdRect box) const noexcept {
    TASTY_SEAT_BODY(BlackPattern);
    constexpr Rgb888 kBlack{0x000000u};
    const HdRect on = box.intersect(canvas.bounds());
    if (on.is_empty())
        return;
    const std::optional<PxPlane> plane = canvas.sub(on);
    if (!plane)
        return;
    const std::uint16_t fill = hd_pack565(kBlack);
    for (std::int32_t y = 0; y < on.h; ++y) {
        auto row = plane->row(PlaneRow{y});
        auto px = row.begin();
        for (std::int32_t x = 0; x < on.w; ++x, ++px)
            *px = fill;
    }
}

}
