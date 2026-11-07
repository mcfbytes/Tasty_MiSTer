// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/types.h"
#include "hal/types.h"

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
    WideCoreFrame core_frame{};
    std::int64_t capture_ns = 0;
    MovieFrame movie_frame = kNoMovieFrame;
    hal::HeaderCtr header_ctr{};
    DupReason dup = DupReason::None;
    std::uint8_t pad_[2]{};

    [[nodiscard]] constexpr FrameStampRun run(CoreFrameCount n, DupReason why) const noexcept;
};

struct FrameStampRun {
    WideCoreFrame first{};
    CoreFrameCount count{};
    hal::HeaderCtr first_ctr{};
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
    FrameStamp out = s;
    out.core_frame = s.core_frame + CoreFrameGap::of_count(*this);
    out.header_ctr = s.header_ctr.plus(hal::HeaderCtr::Lag::of_frames(v));
    return out;
}

constexpr FrameStamp CoreFrameCount::before(const FrameStamp& s) const noexcept {
    FrameStamp out = s;
    out.core_frame = s.core_frame - CoreFrameGap::of_count(*this);
    out.header_ctr = s.header_ctr.minus(hal::HeaderCtr::Lag::of_frames(v));
    return out;
}

constexpr FrameStampRun FrameStamp::run(CoreFrameCount n, DupReason why) const noexcept {
    return FrameStampRun{.first = core_frame, .count = n, .first_ctr = header_ctr, .why = why};
}

}
