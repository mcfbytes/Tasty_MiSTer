// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/fixed_str.h"
#include "proto/types.h"

namespace mister::ui {

struct StatusBarFacts {
    bool splash = true;
    bool net_wired = false;
    bool net_wifi = false;
    bool bluetooth = false;
    proto::OsdMask sdram_cfg{};
    FixedStr<24, StrFit::Clip> clock{};
};

}
