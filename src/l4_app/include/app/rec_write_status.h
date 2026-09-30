// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

enum class RecWriteState : std::uint8_t { Idle, Open, Failed, Closed };

struct RecWriteStatus {
    std::uint16_t gen = 0;
    RecWriteState state = RecWriteState::Idle;
    std::uint8_t pad_ = 0;
    std::int32_t err = 0;
    std::uint32_t rows = 0;
    std::uint32_t dropped = 0;
    std::uint32_t syncs = 0;
    std::uint64_t bytes = 0;
};

using RecWriteStatusCell = xthread::Telemetry<RecWriteStatus, SeatTag::RecWrite>;

}  // namespace mister::app
