// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/index_range.h"

namespace mister::infra {

[[nodiscard]] constexpr char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr bool ascii_alnum(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}

[[nodiscard]] constexpr bool ascii_space(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u == ' ' || (u >= 0x09 && u <= 0x0d);
}

[[nodiscard]] constexpr bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i]))
            return false;
    }
    return true;
}

[[nodiscard]] constexpr bool istarts_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(0, p.size()), p);
}

[[nodiscard]] constexpr bool iends_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(s.size() - p.size()), p);
}

[[nodiscard]] constexpr std::optional<ByteOffset> ifind(
    std::string_view hay,
    std::string_view needle
) noexcept {
    if (needle.empty())
        return ByteOffset{0};
    if (hay.size() < needle.size())
        return std::nullopt;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        if (ieq(hay.substr(i, needle.size()), needle))
            return ByteOffset{i};
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool icontains(std::string_view hay, std::string_view needle) noexcept {
    return ifind(hay, needle).has_value();
}

[[nodiscard]] constexpr std::strong_ordering icompare_n(
    std::string_view a,
    std::string_view b,
    ByteCount count
) noexcept {
    for (const ByteOffset i : affine::indices<ByteOffset>(count)) {
        const int ca = i.inside(a) ? static_cast<unsigned char>(ascii_lower(i.element_at(a))) : 0;
        const int cb = i.inside(b) ? static_cast<unsigned char>(ascii_lower(i.element_at(b))) : 0;
        if (ca != cb)
            return ca < cb ? std::strong_ordering::less : std::strong_ordering::greater;
        if (ca == 0)
            return std::strong_ordering::equal;
    }
    return std::strong_ordering::equal;
}

[[nodiscard]] constexpr std::weak_ordering icompare(
    std::string_view a,
    std::string_view b
) noexcept {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        const int d = static_cast<unsigned char>(ascii_lower(a[i]))
            - static_cast<unsigned char>(ascii_lower(b[i]));
        if (d != 0)
            return d < 0 ? std::weak_ordering::less : std::weak_ordering::greater;
    }
    if (a.size() == b.size())
        return std::weak_ordering::equivalent;
    return a.size() < b.size() ? std::weak_ordering::less : std::weak_ordering::greater;
}

}
