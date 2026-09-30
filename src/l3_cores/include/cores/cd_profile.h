// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/cd_io_indices.h"
#include "cores/cd_quirk_row.h"
#include "cores/cd_tick_periods.h"
#include "infra/seat.h"
#include "proto/reset_terms.h"
#include "svc/disc_engine.h"

namespace mister::cores {

enum class CdDialect : std::uint8_t {
    SegaBcdNibble,
    PceScsi,
    SaturnRing,
    ThreeDo,
    SectorServed,
};

struct CdProfile {
    TASTY_SEAT_EXEMPT(const_shared);
    CdDialect dialect;
    std::uint8_t crc_start;

    bool status_carries_is_data = true;
    bool ready_probe_skips_mode1 = true;
    bool stream_on_data_track = false;
    CdIoIndices io;
    svc::CuePolicy cue;
    CdTickPeriods ticks;

    bool sniff_sega_disc_system = false;

    struct SeekFastPath {
        std::uint32_t count;
        std::uint32_t latency_ticks;
    };
    std::span<const SeekFastPath> seek_fast_paths;

    std::span<const CdQuirkRow> quirks{};

    proto::ResetTerms reset = proto::ResetTerms::framework();
};

}  // namespace mister::cores
