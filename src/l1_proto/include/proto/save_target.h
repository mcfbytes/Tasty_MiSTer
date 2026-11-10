// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/seat.h"

namespace mister::proto {

struct SaveTarget {
    TASTY_SEAT_EXEMPT(component);
    std::string_view core_name;
    std::string_view rom_path;
    bool arcade = false;
};

}
