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

[[nodiscard]] constexpr const char* dup_reason_name(DupReason r) noexcept {
    switch (r) {
        case DupReason::None:
            return "none";
        case DupReason::Missed:
            return "missed";
        case DupReason::Torn:
            return "torn";
        case DupReason::Backpressure:
            return "backpressure";
        case DupReason::Resize:
            return "resize";
        case DupReason::Woven:
            return "woven";
        case DupReason::kCount:
            break;
    }
    return "?";
}

struct FrameStampRun;

struct FrameStamp {
    WideCoreFrame core_frame{};
    std::int64_t capture_ns = 0;
    MovieFrame movie_frame = kNoMovieFrame;
    hal::HeaderCtr header_ctr{};
    DupReason dup = DupReason::None;
    std::uint8_t pad_[2]{};

    [[nodiscard]] constexpr FrameStampRun run(CoreFrameCount n, DupReason why) const noexcept;

    [[nodiscard]] constexpr FrameStamp advanced(CoreFrameCount n) const noexcept;
    [[nodiscard]] constexpr FrameStamp rewound(CoreFrameCount n) const noexcept;
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
        return start().advanced(i);
    }

    [[nodiscard]] constexpr FrameStamp after() const noexcept { return stamp_at(count); }

    [[nodiscard]] constexpr bool precedes(const FrameStampRun& next) const noexcept {
        return after().core_frame == next.first;
    }
};

constexpr FrameStamp FrameStamp::advanced(CoreFrameCount n) const noexcept {
    FrameStamp out = *this;
    out.core_frame = core_frame + CoreFrameGap::of_count(n);
    out.header_ctr = header_ctr.plus(n.header_lag());
    return out;
}

constexpr FrameStamp FrameStamp::rewound(CoreFrameCount n) const noexcept {
    FrameStamp out = *this;
    out.core_frame = core_frame - CoreFrameGap::of_count(n);
    out.header_ctr = header_ctr.minus(n.header_lag());
    return out;
}

constexpr FrameStampRun FrameStamp::run(CoreFrameCount n, DupReason why) const noexcept {
    return FrameStampRun{.first = core_frame, .count = n, .first_ctr = header_ctr, .why = why};
}

}
