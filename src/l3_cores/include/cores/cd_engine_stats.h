// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct CdEngineStats {
    std::uint32_t crc_faults = 0;
    std::uint32_t flow_waits = 0;
    std::uint32_t cdda_sectors = 0;
    std::uint32_t data_sectors = 0;
    std::uint32_t idle_ticks = 0;

    std::uint32_t substitutes = 0;

    std::uint32_t subcode_substitutes = 0;

    std::uint32_t busy_ticks = 0;
};

}  // namespace mister::cores
