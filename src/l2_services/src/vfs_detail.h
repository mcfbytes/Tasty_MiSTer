// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "svc/vfs.h"

namespace mister::svc::detail {

constexpr char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (to_lower(a[i]) != to_lower(b[i])) return false;
    }
    return true;
}

constexpr bool istarts_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(0, p.size()), p);
}

constexpr bool iends_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(s.size() - p.size()), p);
}

constexpr std::size_t ifind(std::string_view hay, std::string_view needle) noexcept {
    if (needle.empty()) return 0;
    if (hay.size() < needle.size()) return std::string_view::npos;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        if (ieq(hay.substr(i, needle.size()), needle)) return i;
    }
    return std::string_view::npos;
}

constexpr int icompare_n(std::string_view a, std::string_view b, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const char ca = (i < a.size()) ? to_lower(a[i]) : '\0';
        const char cb = (i < b.size()) ? to_lower(b[i]) : '\0';
        if (ca != cb) return (ca < cb) ? -1 : 1;
        if (ca == '\0') return 0;
    }
    return 0;
}

inline std::string compose(std::string_view root, std::string_view path) {
    if (!path.empty() && path.front() == '/') return std::string(path);
    std::string out(root);
    while (out.size() > 1 && out.back() == '/')
        out.pop_back();
    out.push_back('/');
    out.append(path);
    return out;
}

struct ZipSplit {
    bool zipped = false;
    std::string_view archive;
    std::string_view member;
};

constexpr ZipSplit zip_split(std::string_view path) noexcept {
    const std::size_t z = ifind(path, ".zip");
    if (z == std::string_view::npos) return {};
    const std::size_t end = z + 4;
    ZipSplit s;
    s.zipped = true;
    s.archive = path.substr(0, end);
    s.member = (end < path.size()) ? path.substr(end + 1) : std::string_view{};
    return s;
}

constexpr bool has_nested_zip(std::string_view path) noexcept {
    const ZipSplit s = zip_split(path);
    if (!s.zipped) return false;
    return ifind(path.substr(s.archive.size()), ".zip") != std::string_view::npos;
}

constexpr std::optional<std::string_view> relative_member(std::string_view folder,
                                                          std::string_view path) noexcept {
    if (folder.empty()) return path;
    if (!istarts_with(path, folder)) return std::nullopt;
    const std::string_view sub = path.substr(folder.size());
    if (!sub.empty() && sub.front() == '/') return sub.substr(1);
    return std::nullopt;
}

constexpr bool in_same_folder(std::string_view folder, std::string_view path) noexcept {
    const std::size_t p = path.rfind('/');
    const std::size_t len = (p == std::string_view::npos) ? 0 : p;
    return folder.size() == len && icompare_n(path, folder, len) == 0;
}

constexpr std::optional<std::uint32_t> scan_hex8(std::string_view f) noexcept {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
    };
    const auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
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
        if (budget < 3 || i + 2 >= f.size() || hex_val(f[i + 2]) < 0) return std::nullopt;
        i += 2;
        budget -= 2;
    }

    std::uint32_t v = 0;
    std::size_t digits = 0;
    while (budget != 0 && i < f.size()) {
        const int d = hex_val(f[i]);
        if (d < 0) break;
        v = static_cast<std::uint32_t>(v * 16u + static_cast<std::uint32_t>(d));
        ++digits;
        ++i;
        --budget;
    }
    if (digits == 0) return std::nullopt;
    return negate ? static_cast<std::uint32_t>(0u - v) : v;
}

enum class Admit : std::uint8_t {
    Reject,
    KeepFile,
    KeepDir,
};

Admit admit_entry(std::string_view name, bool is_dir, bool at_root, const ScanFilter& f);

struct ZipMember {
    std::string_view name;
    std::uint64_t size = 0;
    bool is_dir = false;
};

std::vector<DirEntry> zip_folder_entries(std::string_view folder,
                                         std::span<const ZipMember> members, const ScanFilter& f);

bool dirent_less(const DirEntry& a, const DirEntry& b) noexcept;

}  // namespace mister::svc::detail
