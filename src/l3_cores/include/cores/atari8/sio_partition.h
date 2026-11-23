// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/atari8/sio_types.h"
#include "svc/sector_types.h"

namespace mister::cores::atari8 {

inline constexpr svc::SectorSize kCardSector{512};

struct SioPartition {
    svc::SectorIndex first{};
    svc::SectorIndex meta{};
    svc::SectorSize size{};
    svc::SectorCount count{};
    SioPartitionId id{};
    bool read_only = false;
    bool has_meta = false;
};

}
