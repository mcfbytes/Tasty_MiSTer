// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/status_register.h"

namespace mister::proto {

struct StatusSnapshot {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t stflg = 0;
    std::array<std::uint16_t, StatusRegister::kWords> words{};
    std::uint16_t gen = 0;
};

static_assert(std::is_trivially_copyable_v<StatusSnapshot>);
using StatusSnapshotCell = xthread::Telemetry<StatusSnapshot, SeatTag::Io>;

}
