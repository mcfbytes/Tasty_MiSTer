// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/raw_monotonic_clock.h"

namespace mister::reactor {

struct FrameRecord {

    struct Stamp {
        os::RawMonotonicClock::time_point at{};

        constexpr Stamp() noexcept : at() {}

        constexpr Stamp(os::RawMonotonicClock::time_point t) noexcept : at(t) {}

        constexpr operator os::RawMonotonicClock::time_point() const noexcept { return at; }
    };

    hal::FrameSeq seq{};
    Stamp edge{};
};

using FrameCell = xthread::Telemetry<FrameRecord, SeatTag::Frame>;

}
