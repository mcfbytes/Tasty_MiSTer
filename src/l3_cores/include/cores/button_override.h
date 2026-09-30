// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::cores {

struct ButtonOverride {
    std::string_view names;
    std::string_view defaults;
};

}  // namespace mister::cores
