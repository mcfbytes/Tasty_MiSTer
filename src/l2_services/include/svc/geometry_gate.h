// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/scaling_policy.h"
#include "svc/video_sample.h"

namespace mister::svc {

struct GeometryGate {
    ResWord last_res{};
    FbParCrc last_fb_crc{};
    FltFlags last_flt_flags{};
    bool vi_seen = false;
    VideoSample last{};
    ScalingPolicy policy{};
};

}
