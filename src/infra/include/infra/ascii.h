// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string_view>

namespace mister::infra {

struct ByteCount {
    std::size_t v = 0;

    [[nodiscard]] static constexpr ByteCount of(std::uint64_t bytes) noexcept {
        return ByteCount{static_cast<std::size_t>(bytes)};
    }

    struct Indices {
        std::size_t v = 0;

        [[nodiscard]] constexpr auto begin() const noexcept {
            return std::views::iota(std::size_t{0}, v).begin();
        }

        [[nodiscard]] constexpr auto end() const noexcept {
            return std::views::iota(std::size_t{0}, v).end();
        }
    };

    [[nodiscard]] constexpr Indices indices() const noexcept { return Indices{v}; }

    friend constexpr bool operator==(ByteCount, ByteCount) = default;
    friend constexpr auto operator<=>(ByteCount, ByteCount) = default;
};

struct TextOffset {
    std::size_t v = 0;

    [[nodiscard]] constexpr TextOffset advanced(ByteCount count) const noexcept {
        return TextOffset{v + count.v};
    }

    [[nodiscard]] constexpr std::string_view prefix_of(std::string_view path) const noexcept {
        return path.substr(0, v);
    }

    [[nodiscard]] constexpr std::string_view suffix_after(std::string_view path) const noexcept {
        return path.substr(v);
    }

    [[nodiscard]] constexpr bool inside(std::string_view path) const noexcept {
        return v < path.size();
    }

    friend constexpr bool operator==(TextOffset, TextOffset) = default;
    friend constexpr auto operator<=>(TextOffset, TextOffset) = default;
};

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

[[nodiscard]] constexpr std::optional<TextOffset> ifind(
    std::string_view hay,
    std::string_view needle
) noexcept {
    if (needle.empty())
        return TextOffset{0};
    if (hay.size() < needle.size())
        return std::nullopt;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        if (ieq(hay.substr(i, needle.size()), needle))
            return TextOffset{i};
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
    for (const std::size_t i : count.indices()) {
        const int ca = i < a.size() ? static_cast<unsigned char>(ascii_lower(a[i])) : 0;
        const int cb = i < b.size() ? static_cast<unsigned char>(ascii_lower(b[i])) : 0;
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
