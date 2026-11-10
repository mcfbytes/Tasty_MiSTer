// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/fixed_str.h"

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

inline constexpr std::size_t kCoreRelChars = 128;
using CoreRelative = FixedStr<kCoreRelChars, StrFit::Reject>;

[[nodiscard]] constexpr std::optional<CoreRelative> core_relative(
    std::string_view core_dir,
    std::string_view name
) noexcept {
    CoreRelative out{};
    if (!out.assign(core_dir))
        return std::nullopt;
    if (!out.append("/"))
        return std::nullopt;
    if (!out.append(name))
        return std::nullopt;
    return out;
}

}
