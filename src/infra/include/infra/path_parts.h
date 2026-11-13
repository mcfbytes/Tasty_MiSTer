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

[[nodiscard]] constexpr std::string_view dir_or_self(std::string_view path) noexcept {
    return dirname(path).value_or(path);
}

[[nodiscard]] constexpr std::string_view dir_or_empty(std::string_view path) noexcept {
    return dirname(path).value_or(std::string_view{});
}

[[nodiscard]] constexpr std::string_view dir_prefix(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash + 1);
}

[[nodiscard]] constexpr std::optional<std::string_view> nested_dir(std::string_view path) noexcept {
    const std::optional<std::string_view> dir = dirname(path);
    return dir && !dir->empty() ? dir : std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::string_view> leaf_in_dir(
    std::string_view path
) noexcept {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos)
        return std::nullopt;
    return path.substr(slash + 1);
}

[[nodiscard]] constexpr std::string_view dir_prefix_any_sep(std::string_view path) noexcept {
    const std::size_t sep = path.find_last_of("/\\");
    return sep == std::string_view::npos ? std::string_view{} : path.substr(0, sep + 1);
}

[[nodiscard]] constexpr std::string_view leaf_any_sep(std::string_view path) noexcept {
    const std::size_t sep = path.find_last_of("/\\");
    return sep == std::string_view::npos ? path : path.substr(sep + 1);
}

[[nodiscard]] constexpr std::string_view extension_any_sep(std::string_view path) noexcept {
    const std::string_view name = leaf_any_sep(path);
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : name.substr(dot);
}

[[nodiscard]] constexpr std::string_view stem_any_sep(std::string_view path) noexcept {
    const std::string_view name = leaf_any_sep(path);
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

[[nodiscard]] constexpr std::string_view extension_bare(std::string_view path) noexcept {
    const std::string_view ext = extension(path);
    return ext.empty() ? ext : ext.substr(1);
}

[[nodiscard]] constexpr std::string_view cut_at_last_dot(std::string_view path) noexcept {
    const std::size_t dot = path.rfind('.');
    return dot == std::string_view::npos ? path : path.substr(0, dot);
}

[[nodiscard]] constexpr std::string_view suffix_from_last_dot(std::string_view path) noexcept {
    const std::size_t dot = path.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : path.substr(dot);
}

}
