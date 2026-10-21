// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

namespace mister::svc::xml {

inline constexpr std::size_t kMaxAttrs = 16;

[[nodiscard]] constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] std::string_view rbf_text(std::string_view xml) noexcept;

}
