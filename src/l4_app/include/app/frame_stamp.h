// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

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

struct FrameStamp {
    std::uint64_t core_frame = 0;
    std::int64_t capture_ns = 0;
    std::int32_t movie_frame = -1;
    std::uint8_t header_ctr = 0;
    DupReason dup = DupReason::None;
    std::uint8_t pad_[2]{};
};

struct FrameStampRun {
    std::uint64_t first = 0;
    std::uint32_t count = 0;
    std::uint8_t first_ctr = 0;
    DupReason why = DupReason::Missed;
    std::uint8_t pad_[2]{};
};

}  // namespace mister::app
