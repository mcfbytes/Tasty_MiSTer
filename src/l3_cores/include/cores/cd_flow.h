// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct CdFlow {
    std::uint32_t waits = 0;
    std::uint32_t cdda_sectors = 0;

    std::uint32_t data_sectors = 0;
    std::uint32_t idle_ticks = 0;

    std::uint32_t gated_ticks = 0;
    std::uint8_t drive_state = 0;
    std::uint32_t drive_track = 0;
    std::int32_t drive_lba = 0;

    std::int32_t drive_audio_lba = 0;

    std::uint32_t substitutes = 0;
    std::uint32_t subcode_substitutes = 0;
    std::uint32_t not_resident = 0;
    bool drive_is_data = true;

    std::uint32_t busy_ticks = 0;
    std::uint32_t egress_abandons = 0;
};

}  // namespace mister::cores
