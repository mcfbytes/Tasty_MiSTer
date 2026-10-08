// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct FileTxLevel {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t act = 0;
    bool open = false;
    bool payload = false;
    std::uint8_t pad_[2]{};
};

using FileTxLevelCell = xthread::Telemetry<FileTxLevel, SeatTag::RT>;

}  // namespace mister::app
