// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "hal/sys_block.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/status_word.h"

namespace mister::proto {

struct StatusSnapshot {
    TASTY_SEAT_EXEMPT(const_shared);
    hal::SysBlock::Stflg stflg{};
    StatusWord words{};
    std::uint16_t gen = 0;
};

static_assert(std::is_trivially_copyable_v<StatusSnapshot>);
using StatusSnapshotCell = xthread::Telemetry<StatusSnapshot, SeatTag::Io>;

}
