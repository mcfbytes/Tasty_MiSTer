// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct VideoSample;
struct ScalingPolicy;

struct ScalingWords {
    std::uint16_t height = 0;
    std::uint16_t width = 0;
};
[[nodiscard]] ScalingWords scaling_words(const VideoSample& s, const ScalingPolicy& pol) noexcept;

}  // namespace mister::svc
