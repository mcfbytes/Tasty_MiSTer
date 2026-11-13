// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::neogeo {

struct WalkTally {
    std::uint32_t skipped = 0;
    std::uint32_t config = 0;
};

}
