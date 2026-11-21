// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/video/scaling_policy.h"
#include "svc/video/video_sample.h"

namespace mister::svc::video {

struct GeometryGate {
    ResWord last_res{};
    FbParCrc last_fb_crc{};
    FltFlags last_flt_flags{};
    bool vi_seen = false;
    VideoSample last{};
    ScalingPolicy policy{};
};

}
