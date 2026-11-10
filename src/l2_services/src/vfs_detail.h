// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "infra/ascii.h"
#include "svc/asset_name.h"

namespace mister::svc::detail {

inline std::string compose(std::string_view root, std::string_view path) {
    if (!path.empty() && path.front() == '/')
        return std::string(path);
    std::string out(root);
    while (out.size() > 1 && out.back() == '/')
        out.pop_back();
    out.push_back('/');
    out.append(path);
    return out;
}

inline bool has_nested_zip(std::string_view path) noexcept {
    const asset_name::ZipSplit s = asset_name::zip_split(path);
    if (!s.zipped)
        return false;
    return infra::ifind(path.substr(s.archive.size()), ".zip").has_value();
}

constexpr std::optional<std::string_view> relative_member(
    std::string_view folder,
    std::string_view path
) noexcept {
    if (folder.empty())
        return path;
    if (!infra::istarts_with(path, folder))
        return std::nullopt;
    const std::string_view sub = path.substr(folder.size());
    if (!sub.empty() && sub.front() == '/')
        return sub.substr(1);
    return std::nullopt;
}

constexpr bool in_same_folder(std::string_view folder, std::string_view path) noexcept {
    const std::size_t p = path.rfind('/');
    const std::size_t len = (p == std::string_view::npos) ? 0 : p;
    return folder.size() == len && infra::icompare_n(path, folder, infra::ByteCount{len}) == 0;
}

constexpr std::optional<std::uint32_t> scan_hex8(std::string_view f) noexcept {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
    };
    const auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    std::size_t i = 0;
    while (i < f.size() && is_space(f[i]))
        ++i;

    std::size_t budget = 8;
    bool negate = false;
    if (i < f.size() && (f[i] == '+' || f[i] == '-')) {
        negate = (f[i] == '-');
        ++i;
        --budget;
    }

    if (budget >= 2 && i + 1 < f.size() && f[i] == '0' && (f[i + 1] == 'x' || f[i + 1] == 'X')) {
        if (budget < 3 || i + 2 >= f.size() || hex_val(f[i + 2]) < 0)
            return std::nullopt;
        i += 2;
        budget -= 2;
    }

    std::uint32_t v = 0;
    std::size_t digits = 0;
    while (budget != 0 && i < f.size()) {
        const int d = hex_val(f[i]);
        if (d < 0)
            break;
        v = static_cast<std::uint32_t>(v * 16u + static_cast<std::uint32_t>(d));
        ++digits;
        ++i;
        --budget;
    }
    if (digits == 0)
        return std::nullopt;
    return negate ? static_cast<std::uint32_t>(0u - v) : v;
}

}
