// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::ui {

struct StatusBarFacts {
    bool splash = true;
    bool net_wired = false;
    bool net_wifi = false;
    bool bluetooth = false;
    std::uint16_t sdram_cfg = 0;
    char clock[24] = {};
};

}
