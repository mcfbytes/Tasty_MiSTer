// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

enum class OcramRunEdge : std::uint8_t {
    Raise = 0,
    Drop = 1,
};

}
