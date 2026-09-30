// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct KeyEdge {
    std::uint16_t code = 0;
    bool pressed = false;
};

}  // namespace mister::svc
