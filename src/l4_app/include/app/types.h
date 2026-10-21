// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::app {

using proto::CorrelationTag;
using proto::kUncaused;

struct EmitSite {
    std::uint16_t v = 0;
    friend constexpr bool operator==(EmitSite, EmitSite) = default;
};

enum class ContentEdge : std::uint8_t {
    Content = 0,
    Silent = 1,
    ChainEnd = 2,
};

}
