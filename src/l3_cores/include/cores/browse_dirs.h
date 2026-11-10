// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::cores {

struct BrowseDirs {
    std::string_view dir;
    std::string_view home;
};

}
