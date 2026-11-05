// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/arc_label.h"

#include <cstdio>
#include <iterator>
#include <string_view>

namespace mister::ui {

std::string resolve_arc(std::string_view label, const std::string_view (&arc)[2]) {
    constexpr std::string_view kTags[] = {"[ARC1]", "[ARC2]"};
    constexpr unsigned kMaxDimension = 4095;
    std::size_t which = 0;
    while (which < std::size(kTags) && label != kTags[which])
        ++which;
    if (which == std::size(kTags))
        return std::string{label};

    const std::string_view s = arc[which];
    unsigned x = 0, y = 0;

    const std::string tmp{s};
    if (std::sscanf(tmp.c_str(), "%u:%u", &x, &y) != 2
        || x < 1
        || x > kMaxDimension
        || y < 1
        || y > kMaxDimension) {
        return std::string{};
    }
    return tmp;
}

}
