// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::app {

struct CheatRow {
    std::string_view name;
    bool enabled = false;
};

}  // namespace mister::app
