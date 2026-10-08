// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/fixed_str.h"
#include "infra/json_out.h"
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

inline constexpr std::array<const char*, static_cast<std::size_t>(ReplayEnd::kCount)>
    kReplayEndNames{"none",     "finished", "stopped", "superseded", "switch",
                    "ref_lost", "no_ref",   "epoch",   "refused"};
static_assert(infra::json_clean_names(kReplayEndNames), "a name is rendered unescaped");

[[nodiscard]] constexpr const char* replay_end_name(ReplayEnd e) noexcept {
    const auto i = static_cast<std::size_t>(e);
    return i < kReplayEndNames.size() ? kReplayEndNames[i] : "?";
}

constexpr void to_json(infra::JsonOut& o, const ReplayStatus& s) noexcept {
    o.field("gen", s.gen);
    o.field("lvl", s.level);
    o.str("end", replay_end_name(s.end), infra::json_longest(kReplayEndNames));
    o.field("ref", s.ref);
    o.field("frame", s.movie_frame);
    o.field("applied", s.applied_frame);
    o.field("writes", s.writes);
    o.field("on_time", s.on_time);
    o.field("late", s.late);
    o.field("first_late", s.first_late_frame);
    o.field("underruns", s.underruns);
    o.field("first_underrun", s.first_underrun_frame);
    o.field("pre_epoch", s.pre_epoch);
    o.field("skipped", s.skipped_edges);
    o.field("held", s.held_rounds);
    o.field("drops", s.pad_drops);
    o.field("stale", s.stale);
    o.field("link_err", s.link_errors);
    o.field("min_us", s.apply_min_us);
    o.field("max_us", s.apply_max_us);
    o.field("epoch_us", s.epoch_delay_us);
    o.field("epoch", s.epoch_edge);
    o.field("blk_late", s.blk_late);
    o.field("blk_max_us", s.blk_late_max_us);
    o.field("lost", s.lost);
    o.field("delayed", s.delayed);
    o.field("gap_max_us", s.gap_max_us);
    o.field("gap_frame", s.gap_frame);
    o.field("late_gap_us", s.late_gap_us);
    o.field("late_into_us", s.late_into_us);
    o.field("late_edge_us", s.late_edge_us);
    o.field("late_depth", s.late_depth);
    o.field("depth_min", s.depth_min);
    o.field("depth_min_frame", s.depth_min_frame);
}

using ReplayStatusCell = xthread::Telemetry<ReplayStatus, SeatTag::RT>;

using ReplayStatusText = FixedStr<832, StrFit::Clip>;
static_assert(infra::json_worst_len<ReplayStatus>() <= ReplayStatusText::kCapacity,
              "the status text never clips");
[[nodiscard]] ReplayStatusText format_replay_status(const ReplayStatus& s) noexcept;

[[nodiscard]] const char* replay_end_sentence(ReplayEnd e,
                                              EpochFail fail = EpochFail::None) noexcept;

}  // namespace mister::app
