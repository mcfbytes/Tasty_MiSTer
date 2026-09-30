// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "svc/pll_solver.h"

namespace mister::svc {

struct Modeline {
    std::uint32_t hact = 0, hfp = 0, hs = 0, hbp = 0;
    std::uint32_t vact = 0, vfp = 0, vs = 0, vbp = 0;
    double fpix_mhz = 0.0;
    bool hpol = false, vpol = false;
    std::uint8_t vic = 0;
    std::uint8_t rb = 0;
    bool pixel_repeat = false;

    std::array<std::uint32_t, 26> to_words(const PllBlock& pll) const;

    struct WireOptions {
        bool use_vrr = false;
        bool vsync_align = false;

        bool direct_video = false;
    };

    std::array<std::uint16_t, 26> to_wire(const PllBlock& pll, const WireOptions& opt) const;

    Modeline direct_video_fix() const;
};

}  // namespace mister::svc
