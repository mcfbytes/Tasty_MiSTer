// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace mister::infra {

[[nodiscard]] constexpr std::string_view basename(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] constexpr std::string_view extension(std::string_view path) noexcept {
    const std::string_view name = basename(path);
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : name.substr(dot);
}

[[nodiscard]] constexpr std::string_view stem(std::string_view path) noexcept {
    const std::string_view name = basename(path);
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

[[nodiscard]] constexpr std::optional<std::string_view> dirname(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos)
        return std::nullopt;
    return path.substr(0, slash);
}

}
