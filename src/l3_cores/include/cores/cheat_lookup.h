// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/seat.h"

namespace mister::cores {

struct CheatLookup {
    TASTY_SEAT_EXEMPT(const_shared);

    std::string_view cd_dir_suffix{};

    bool cd_asset_layout = false;

    bool reset_on_remount = false;

    bool reset_on_mount = true;
};

}
