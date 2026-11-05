// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::ui {

struct StatusBarFacts {
    bool splash = true;
    bool net_wired = false;
    bool net_wifi = false;
    bool bluetooth = false;
    proto::OsdMask sdram_cfg{};
    char clock[24] = {};
};

}
