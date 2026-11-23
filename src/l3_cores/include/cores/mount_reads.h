// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/mount_serial.h"
#include "cores/read_count.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::cores {

struct MountReads {
    MountSerial mount{};
    ReadCount data_reads{};
    bool translated = false;

    friend constexpr bool operator==(const MountReads&, const MountReads&) = default;
};

using MountReadsCell = xthread::Telemetry<MountReads, SeatTag::Io>;

}
