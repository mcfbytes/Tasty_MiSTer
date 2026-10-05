// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

enum class RecState : std::uint8_t { Idle, Probing, Armed, Recording, Closing };

enum class RecVerdict : std::uint8_t {
    None,
    Started,
    Stopped,
    NoWindow,
    NoLiveBuffer,
    Busy,
    WriterFailed,
    CoreSwitch,
    ReplayEnded,
    NoCodec,
    NoMemory,

    ScalerPortStuck,
    kCount,
};

[[nodiscard]] const char* rec_state_name(RecState s) noexcept;
[[nodiscard]] const char* rec_verdict_name(RecVerdict v) noexcept;

[[nodiscard]] const char* rec_verdict_remedy(RecVerdict v) noexcept;

struct RecorderStatus {
    std::uint16_t gen = 0;
    std::uint16_t answered = 0;
    RecState state = RecState::Idle;
    RecVerdict verdict = RecVerdict::None;
    RecVerdict end = RecVerdict::None;
    std::uint8_t stride_mib = 0;
    std::uint8_t lowlat = 0;
    std::uint8_t anchored = 0;
    std::uint8_t avi = 0;
    std::uint8_t interlaced = 0;
    std::uint8_t depth = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t rows = 0;
    std::uint32_t captured = 0;
    std::uint32_t missed = 0;
    std::uint32_t torn = 0;
    std::uint32_t backpressure = 0;
    std::uint32_t resize = 0;
    std::uint32_t gap_estimated = 0;
    std::uint32_t stamp_drift = 0;
    std::uint32_t rebased = 0;
    std::uint32_t bad_header = 0;
    std::uint32_t woven = 0;
    std::uint32_t unmatched = 0;
    std::uint32_t copy_us_last = 0;
    std::uint32_t copy_us_max = 0;
    std::uint32_t arena_kib = 0;
    std::uint64_t first_core_frame = 0;
    std::uint64_t last_core_frame = 0;
};

using RecorderStatusCell = xthread::Telemetry<RecorderStatus, SeatTag::Capture>;

using RecStatusText = FixedStr<640, StrFit::Clip>;

[[nodiscard]] RecStatusText format_recorder_status(const RecorderStatus& s) noexcept;

}  // namespace mister::app
