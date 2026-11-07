// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/scaling_policy.h"
#include "svc/video_sample.h"

namespace mister::svc {

struct GeometryGate {
    ResWord last_res{};
    std::uint8_t last_fb_crc = 0;
    std::uint16_t last_flt_flags = 0;
    bool vi_seen = false;
    VideoSample last{};
    ScalingPolicy policy{};
};

}
