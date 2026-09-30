// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/rec_write_status.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct AviWriteStatus {
    std::uint16_t gen = 0;
    RecWriteState state = RecWriteState::Idle;
    std::uint8_t pad_ = 0;
    std::int32_t err = 0;
    std::uint16_t segment = 0;
    std::uint16_t segments = 0;
    std::uint32_t frames = 0;
    std::uint32_t dropped = 0;
    std::uint32_t header_rewrites = 0;
    std::uint32_t syncs = 0;
    std::uint64_t bytes = 0;
};

using AviWriteStatusCell = xthread::Telemetry<AviWriteStatus, SeatTag::RecWrite>;

}  // namespace mister::app
