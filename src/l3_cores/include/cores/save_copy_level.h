// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::cores {

struct SaveCopyLevel {
    TASTY_SEAT_EXEMPT(const_shared);
    bool writing = false;
};
static_assert(std::is_trivially_copyable_v<SaveCopyLevel>);

using SaveCopyCell = xthread::Telemetry<SaveCopyLevel, SeatTag::Ui>;

}  // namespace mister::cores
