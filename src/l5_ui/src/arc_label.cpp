// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/arc_label.h"

#include <cstdio>

namespace mister::ui {

std::string resolve_arc(std::string_view label, const std::string_view (&arc)[2]) {
    int which = 0;
    if (label == "[ARC1]")
        which = 1;
    else if (label == "[ARC2]")
        which = 2;
    else
        return std::string{label};

    const std::string_view s = arc[which - 1];
    unsigned x = 0, y = 0;

    const std::string tmp{s};
    if (std::sscanf(tmp.c_str(), "%u:%u", &x, &y) != 2 || x < 1 || x > 4095 || y < 1 || y > 4095) {
        return std::string{};
    }
    return tmp;
}

}
