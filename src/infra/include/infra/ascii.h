// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

namespace mister::infra {

[[nodiscard]] constexpr char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

[[nodiscard]] constexpr bool istarts_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(0, p.size()), p);
}

[[nodiscard]] constexpr bool iends_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(s.size() - p.size()), p);
}

}  // namespace mister::infra
