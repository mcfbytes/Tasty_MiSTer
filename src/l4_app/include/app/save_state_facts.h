// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct SaveStateFacts {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t answered = 0;
    std::uint32_t saves_written = 0;
    std::uint32_t refusals = 0;
    std::uint32_t write_failures = 0;
    std::uint32_t prime_aborts = 0;
    bool live = false;
};

static_assert(std::is_trivially_copyable_v<SaveStateFacts>);

using SaveStateFactsCell = xthread::Telemetry<SaveStateFacts, SeatTag::Ui>;

}
