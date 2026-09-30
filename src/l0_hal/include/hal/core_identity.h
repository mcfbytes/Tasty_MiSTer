// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

struct CoreIdentity {
    std::uint8_t type_byte = 0x55;
    bool dual_sdram = false;
};

}  // namespace mister::hal
