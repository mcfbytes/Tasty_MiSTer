// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/cd_drive_state.h"
#include "cores/types.h"
#include "svc/types.h"

namespace mister::cores {

struct CdFlow {
    std::uint32_t waits = 0;
    std::uint32_t cdda_sectors = 0;

    std::uint32_t data_sectors = 0;
    std::uint32_t idle_ticks = 0;

    std::uint32_t gated_ticks = 0;

    CdDriveState drive_state = CdDriveState::NoDisc;
    svc::TrackIndex drive_track{};
    HeadLba drive_lba{};

    HeadLba drive_audio_lba{};

    std::uint32_t substitutes = 0;
    std::uint32_t subcode_substitutes = 0;
    std::uint32_t not_resident = 0;
    bool drive_is_data = true;

    std::uint32_t busy_ticks = 0;
    std::uint32_t egress_abandons = 0;
};

}
