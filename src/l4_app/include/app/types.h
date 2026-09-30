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

}  // namespace mister::app
