// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace mister::infra {

template <class T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
[[nodiscard]] inline std::optional<T> parse_int(std::string_view s, int base = 10) noexcept {
    if (s.empty())
        return std::nullopt;
    T v{};
    const char* const last = s.data() + s.size();
    const auto [p, ec] = std::from_chars(s.data(), last, v, base);
    if (ec != std::errc{} || p != last)
        return std::nullopt;
    return v;
}

[[nodiscard]] constexpr std::string_view trim_line_end(std::string_view s) noexcept {
    while (!s.empty()
           && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

}
