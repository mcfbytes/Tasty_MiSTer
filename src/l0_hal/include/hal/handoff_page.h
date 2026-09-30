// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

struct HandoffPage {
    bool env_valid = false;
    bool sdram_valid = false;
    std::uint16_t sdram_cfg = 0;
    bool altcfg_valid = false;
    std::uint8_t altcfg = 0;
    std::uint32_t reboot_flag = 0;
};

}  // namespace mister::hal
