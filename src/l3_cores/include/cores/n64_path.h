// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::cores::n64 {

[[nodiscard]] constexpr std::string_view path_leaf(std::string_view path) noexcept {
    const auto cut = path.find_last_of("/\\");
    return cut == std::string_view::npos ? path : path.substr(cut + 1);
}

[[nodiscard]] constexpr bool same_file(std::string_view a, std::string_view b) noexcept {
    if (a.empty() || b.empty())
        return false;
    if (a == b)
        return true;
    const std::string_view longer = a.size() > b.size() ? a : b;
    const std::string_view shorter = a.size() > b.size() ? b : a;
    return longer.ends_with(shorter) && longer[longer.size() - shorter.size() - 1] == '/';
}

}
