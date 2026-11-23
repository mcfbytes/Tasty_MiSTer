// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "os/raw_monotonic_clock.h"
#include "svc/sector_types.h"

namespace mister::svc {

enum class SectorDrive : std::uint8_t { Model810, Model1050 };

enum class SectorCheck : std::uint8_t { None, Verify };

struct SectorAsk {
    SectorIndex sector{};

    os::RawMonotonicClock::time_point at{};
    SectorDrive drive = SectorDrive::Model810;
    SectorCheck check = SectorCheck::None;
};

}
