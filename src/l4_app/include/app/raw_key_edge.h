// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/types.h"

namespace mister::app {

struct RawKeyEdge {
    svc::EvdevCode code{};
    bool pressed = false;
    std::uint8_t reserved = 0;
};

static_assert(sizeof(RawKeyEdge) == 4, "the ring's slot size is load-bearing");

using KeyDownSet = svc::KeyDownSet<4>;

}
