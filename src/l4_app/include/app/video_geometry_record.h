// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/frame_ticks.h"
#include "app/types.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "svc/video/res_word.h"

namespace mister::app {

struct VideoGeometryRecord {
    bool valid = false;
    bool fb_en = false;

    bool rotated = false;
    svc::video::ResWord res{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    FrameTicks rate{};
    CoreEdgeSeq core_seq{};
};

using VideoGeometryCell = xthread::Telemetry<VideoGeometryRecord, SeatTag::Ui>;

}
