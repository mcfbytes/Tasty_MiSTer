// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::svc {

struct MoveNames {
    std::string_view from;
    std::string_view to;
};

}
