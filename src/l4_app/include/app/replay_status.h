// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

enum class ReplayLevel : std::uint8_t { Idle, AwaitPowerOn, Running };

enum class ReplayEnd : std::uint8_t {
    None,
    Finished,
    Stopped,
    Superseded,
    CoreSwitch,
    RefLost,
    NoReference,
    EpochAmbiguous,
    Refused,
    kCount,
};

enum class ReplayRef : std::uint8_t { None, Counter, Vsync };

enum class EpochFail : std::uint8_t { None = 0, Retry = 1, Untimed = 2, Before = 3 };

struct ReplayStatus {
    std::uint16_t gen = 0;
    ReplayLevel level = ReplayLevel::Idle;
    ReplayEnd end = ReplayEnd::None;
    ReplayRef ref = ReplayRef::None;
    EpochFail epoch_fail = EpochFail::None;
    std::uint8_t pad_[2]{};
    std::int32_t movie_frame = -1;
    std::int32_t applied_frame = -1;
    std::uint32_t writes = 0;
    std::uint32_t on_time = 0;
    std::uint32_t late = 0;
    std::uint32_t pre_epoch = 0;
    std::uint32_t underruns = 0;
    std::uint32_t skipped_edges = 0;
    std::uint32_t held_rounds = 0;
    std::uint32_t pad_drops = 0;
    std::uint32_t stale = 0;
    std::uint32_t link_errors = 0;
    std::int32_t first_late_frame = -1;
    std::int32_t first_underrun_frame = -1;
    std::uint32_t apply_min_us = 0;
    std::uint32_t apply_max_us = 0;
    std::uint32_t epoch_delay_us = 0;
    std::uint32_t epoch_edge = 0;
    std::uint32_t blk_late = 0;
    std::uint32_t blk_late_max_us = 0;
    std::uint32_t lost = 0;
    std::uint32_t delayed = 0;
    std::uint32_t gap_max_us = 0;
    std::int32_t gap_frame = -1;
    std::uint32_t late_gap_us = 0;
    std::uint32_t late_into_us = 0;
    std::uint32_t late_edge_us = 0;
    std::uint32_t late_depth = 0;
    std::int32_t depth_min = -1;
    std::int32_t depth_min_frame = -1;
};

using ReplayStatusCell = xthread::Telemetry<ReplayStatus, SeatTag::RT>;

using ReplayStatusText = FixedStr<832, StrFit::Clip>;
[[nodiscard]] ReplayStatusText format_replay_status(const ReplayStatus& s) noexcept;
[[nodiscard]] const char* replay_end_name(ReplayEnd e) noexcept;

[[nodiscard]] const char* replay_end_sentence(ReplayEnd e,
                                              EpochFail fail = EpochFail::None) noexcept;

}  // namespace mister::app
