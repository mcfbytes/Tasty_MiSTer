// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace mister::proto {

namespace {

struct Scan {
    std::uint32_t value;
    std::size_t pos;
};

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

}

}
