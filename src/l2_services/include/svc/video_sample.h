// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/aspect_raw.h"
#include "svc/fb_par_crc.h"
#include "svc/resolution.h"
#include "svc/res_word.h"
#include "svc/types.h"

namespace mister::svc {

struct VideoSample {
    ResWord res{};
    Resolution active{};
    std::uint32_t htime = 0, vtime = 0, ptime = 0, vtimeh = 0, ctime = 0;
    std::uint16_t pixrep = 0, de_h = 0, de_v = 0;
    std::uint32_t frame_clocks = 0;

    FbParCrc fb_crc{};
    AspectRaw aspect{};
    std::uint16_t fb_fmt = 0, fb_width = 0, fb_height = 0;

    FltFlags flt_flags{};

    [[nodiscard]] bool interlaced() const noexcept { return res.interlaced(); }

    [[nodiscard]] bool rotated() const noexcept { return res.rotated(); }

    [[nodiscard]] bool arxy() const noexcept { return aspect.arxy(); }

    [[nodiscard]] std::uint16_t arx() const noexcept { return aspect.arx(); }

    [[nodiscard]] std::uint16_t ary() const noexcept { return aspect.ary(); }

    [[nodiscard]] bool fb_en() const noexcept {
        constexpr std::uint16_t kFbEnable = 0x40;
        return (fb_fmt & kFbEnable) != 0;
    }
};

static_assert(
    sizeof(ResWord) == 2 && sizeof(AspectRaw) == 4,
    "both are fabric reply words, not widened records"
);

}
