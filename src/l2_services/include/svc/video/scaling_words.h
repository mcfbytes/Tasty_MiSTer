// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::svc::video {

struct VideoSample;
struct ScalingPolicy;

struct ScalingWords {
    hal::LinkWord height{};
    hal::LinkWord width{};
};

[[nodiscard]] ScalingWords scaling_words(const VideoSample& s, const ScalingPolicy& pol) noexcept;

}
