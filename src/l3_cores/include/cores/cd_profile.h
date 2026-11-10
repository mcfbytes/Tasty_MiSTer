// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <type_traits>
#include <variant>

#include "cores/cd_io_indices.h"
#include "cores/cd_quirk_row.h"
#include "cores/cd_tick_periods.h"
#include "cores/types.h"
#include "infra/seat.h"
#include "proto/reset_terms.h"
#include "svc/cue_policy.h"

namespace mister::cores {

struct CdProfile {
    TASTY_SEAT_EXEMPT(const_shared);

    struct SegaCddFacts {
        TASTY_SEAT_EXEMPT(const_shared);

        std::uint8_t crc_start;
        bool status_carries_is_data = true;
        bool ready_probe_skips_mode1 = true;
        bool stream_on_data_track = false;
    };

    struct SeekFastPath {
        SectorCount count;
        std::uint32_t latency_ticks;
    };

    struct PceScsiFacts {
        TASTY_SEAT_EXEMPT(const_shared);

        std::span<const SeekFastPath> seek_fast_paths;
    };

    struct SaturnFacts {
        TASTY_SEAT_EXEMPT(const_shared);

        std::span<const CdQuirkRow> quirks{};
    };

    struct ThreeDoFacts {};

    using DialectRow = std::variant<SegaCddFacts, PceScsiFacts, SaturnFacts, ThreeDoFacts>;
    static_assert(std::variant_size_v<DialectRow> == 4, "the dialect row set is closed");

    CdIoIndices io;
    svc::CuePolicy cue;
    CdTickPeriods ticks;

    proto::ResetTerms reset = proto::ResetTerms::framework();
    DialectRow row;
};

static_assert(
    std::is_trivially_destructible_v<CdProfile>,
    "a manifest is constinit, so the variant must stay in .rodata"
);
static_assert(std::is_trivially_destructible_v<CdProfile::DialectRow>);

}
