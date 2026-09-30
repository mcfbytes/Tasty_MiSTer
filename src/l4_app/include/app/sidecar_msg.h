// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/frame_stamp.h"
#include "app/rec_control.h"
#include "infra/spsc_ring.h"

namespace mister::app {

enum class SidecarKind : std::uint8_t { Open, Row, Close };

struct SidecarMsg {
    SidecarKind kind = SidecarKind::Row;
    std::uint8_t pad_ = 0;
    std::uint16_t gen = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t hash = 0;
    std::int32_t segment = -1;
    std::int32_t seg_frame = -1;
    FrameStamp stamp{};
    RecPath path{};
};

using SidecarRing = xthread::SpscRing<SidecarMsg, 128>;

}  // namespace mister::app
