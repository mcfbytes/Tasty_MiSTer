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

inline constexpr std::array<const char*, 5> kRecStateNames{"idle", "probing", "armed", "recording",
                                                           "closing"};
static_assert(kRecStateNames.size() == static_cast<std::size_t>(RecState::Closing) + 1);
static_assert(infra::json_clean_names(kRecStateNames), "a name is rendered unescaped");

[[nodiscard]] constexpr const char* rec_state_name(RecState s) noexcept {
    const auto i = static_cast<std::size_t>(s);
    return i < kRecStateNames.size() ? kRecStateNames[i] : "?";
}
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

constexpr void to_json(infra::JsonOut& o, const RecorderStatus& s) noexcept {
    o.str("state", rec_state_name(s.state), infra::json_longest(kRecStateNames));
    o.field("gen", s.gen);
    o.field("avi", s.avi);
    o.field("stride_mib", s.stride_mib);
    o.field("lowlat", s.lowlat);
    o.field("anchored", s.anchored);
    o.field("interlaced", s.interlaced);
    o.field("w", s.width);
    o.field("h", s.height);
    o.field("rows", s.rows);
    o.field("captured", s.captured);
    o.field("missed", s.missed);
    o.field("torn", s.torn);
    o.field("backpressure", s.backpressure);
    o.field("resize", s.resize);
    o.field("gaps", s.gap_estimated);
    o.field("drift", s.stamp_drift);
    o.field("rebased", s.rebased);
    o.field("bad_header", s.bad_header);
    o.field("woven", s.woven);
    o.field("unmatched", s.unmatched);
    o.field("copy_us", s.copy_us_last);
    o.field("copy_us_max", s.copy_us_max);
    o.field("arena_kib", s.arena_kib);
    o.field("depth", s.depth);
    o.field("first", s.first_core_frame);
    o.field("last", s.last_core_frame);
}

using RecorderStatusCell = xthread::Telemetry<RecorderStatus, SeatTag::Capture>;

using RecStatusText = FixedStr<640, StrFit::Clip>;

static_assert(infra::json_worst_len<RecorderStatus>() <= RecStatusText::kCapacity,
              "the status text never clips");
[[nodiscard]] RecStatusText format_recorder_status(const RecorderStatus& s) noexcept;

}  // namespace mister::app
