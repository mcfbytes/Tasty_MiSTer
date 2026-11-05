// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/types.h"

namespace mister::app {

enum class DupReason : std::uint8_t {
    None,
    Missed,
    Torn,
    Backpressure,
    Resize,
    Woven,
    kCount,
};

[[nodiscard]] const char* dup_reason_name(DupReason r) noexcept;

struct FrameStampRun;

struct FrameStamp {
    std::uint64_t core_frame = 0;
    std::int64_t capture_ns = 0;
    MovieFrame movie_frame = kNoMovieFrame;
    std::uint8_t header_ctr = 0;
    DupReason dup = DupReason::None;
    std::uint8_t pad_[2]{};

    [[nodiscard]] constexpr FrameStampRun run(CoreFrameCount n, DupReason why) const noexcept;
};

struct FrameStampRun {
    std::uint64_t first = 0;
    CoreFrameCount count{};
    std::uint8_t first_ctr = 0;
    DupReason why = DupReason::Missed;
    std::uint8_t pad_[2]{};

    [[nodiscard]] constexpr FrameStamp start() const noexcept {
        return FrameStamp{.core_frame = first, .header_ctr = first_ctr, .dup = why};
    }

    [[nodiscard]] constexpr FrameStamp stamp_at(CoreFrameCount i) const noexcept {
        return i.after(start());
    }

    [[nodiscard]] constexpr FrameStamp after() const noexcept { return stamp_at(count); }

    [[nodiscard]] constexpr bool precedes(const FrameStampRun& next) const noexcept {
        return after().core_frame == next.first;
    }
};

constexpr FrameStamp CoreFrameCount::after(const FrameStamp& s) const noexcept {
    constexpr unsigned kCtrMask = 0x7;
    FrameStamp out = s;
    out.core_frame = s.core_frame + v;
    out.header_ctr = static_cast<std::uint8_t>((s.header_ctr + v) & kCtrMask);
    return out;
}

constexpr FrameStamp CoreFrameCount::before(const FrameStamp& s) const noexcept {
    constexpr unsigned kCtrMask = 0x7;
    FrameStamp out = s;
    out.core_frame = s.core_frame - v;
    out.header_ctr = static_cast<std::uint8_t>((s.header_ctr - v) & kCtrMask);
    return out;
}

constexpr FrameStampRun FrameStamp::run(CoreFrameCount n, DupReason why) const noexcept {
    return FrameStampRun{.first = core_frame, .count = n, .first_ctr = header_ctr, .why = why};
}

}
