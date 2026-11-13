// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/hd_osd_ask.h"
#include "app/hd_rect.h"
#include "app/hd_theme.h"
#include "app/px_plane.h"
#include "infra/seat.h"

namespace mister::app {

enum class Axis : std::uint8_t { Horizontal, Vertical };

class FrontEndPattern {
public:
    virtual ~FrontEndPattern() = default;

    virtual void paint(PxPlane& canvas, HdRect box) const noexcept = 0;
};

inline constexpr std::uint32_t kCurtain = 0x63000000u;
inline constexpr unsigned kCurtainAlpha = kCurtain >> 24u;

namespace detail {

inline void curtain_pass(PxPlane& canvas, HdRect box) noexcept {
    const HdRect on = box.intersect(canvas.bounds());
    if (on.is_empty())
        return;
    const std::optional<PxPlane> plane = canvas.sub(on);
    if (!plane)
        return;

    constexpr unsigned kKeepPct = (255u - kCurtainAlpha) * 100u / 255u;
    const unsigned keep = kKeepPct;
    for (std::int32_t y = 0; y < on.h; ++y) {
        auto row = plane->row(PlaneRow{y});
        auto px = row.begin();
        for (std::int32_t x = 0; x < on.w; ++x, ++px)
            *px = hd_scale_565(*px, keep);
    }
}

}

inline void curtain(PxPlane& canvas, HdRect box, FrontEndIdle idle) noexcept {
    const bool dim = idle == FrontEndIdle::Dimmed || idle == FrontEndIdle::Dark;
    if (!dim && idle != FrontEndIdle::DarkLogo)
        return;
    detail::curtain_pass(canvas, box);
    detail::curtain_pass(canvas, box);
    if (idle == FrontEndIdle::DarkLogo) {
        detail::curtain_pass(canvas, box);
        detail::curtain_pass(canvas, box);
    }
}

}
