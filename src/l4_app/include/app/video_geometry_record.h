// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct VideoGeometryRecord {
    bool valid = false;
    bool fb_en = false;

    bool rotated = false;
    std::uint16_t res = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t vtime = 0;
    std::uint32_t core_seq = 0;
};

using VideoGeometryCell = xthread::Telemetry<VideoGeometryRecord, SeatTag::Ui>;

}
