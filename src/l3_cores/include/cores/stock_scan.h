// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mister::cores::stock_scan {

[[nodiscard]] constexpr bool is_c_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

[[nodiscard]] constexpr int digit_val(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return -1;
}

struct Scan {
    std::uint32_t value = 0;
    std::size_t used = 0;
};

namespace detail {

struct Parse {
    bool neg = false;
    std::uint64_t mag = 0;
    bool over = false;
    std::size_t used = 0;
    bool any = false;
};

[[nodiscard]] constexpr Parse parse(std::string_view s, int base, std::uint64_t cap) noexcept {
    Parse p;
    std::size_t i = 0;
    while (i < s.size() && is_c_space(s[i]))
        ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        p.neg = (s[i] == '-');
        ++i;
    }
    if (base == 16 || base == 0) {
        if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
            if (i + 2 < s.size() && digit_val(s[i + 2]) >= 0 && digit_val(s[i + 2]) < 16) {
                i += 2;
                base = 16;
            } else {
                p.used = i + 1;
                p.any = true;
                return p;
            }
        } else if (base == 0) {
            base = (i < s.size() && s[i] == '0') ? 8 : 10;
        }
    }
    while (i < s.size()) {
        const int d = digit_val(s[i]);
        if (d < 0 || d >= base)
            break;
        p.any = true;
        if (!p.over) {
            p.mag = p.mag * static_cast<unsigned>(base) + static_cast<unsigned>(d);
            if (p.mag > cap)
                p.over = true;
        }
        ++i;
    }
    if (p.any)
        p.used = i;
    return p;
}

}

[[nodiscard]] constexpr Scan scan_ul(std::string_view s, int base) noexcept {
    const detail::Parse p = detail::parse(s, base, 0xFFFF'FFFFull);
    if (!p.any)
        return Scan{};
    std::uint32_t v = p.over ? 0xFFFF'FFFFu : static_cast<std::uint32_t>(p.mag);
    if (p.neg && !p.over)
        v = 0u - v;
    return Scan{v, p.used};
}

[[nodiscard]] constexpr Scan scan_l(std::string_view s, int base) noexcept {
    const detail::Parse p = detail::parse(s, base, 0x8000'0000ull);
    if (!p.any)
        return Scan{};
    std::uint32_t v = 0;
    if (p.neg) {
        v = (p.over || p.mag >= 0x8000'0000ull) ? 0x8000'0000u
                                                : 0u - static_cast<std::uint32_t>(p.mag);
    } else {
        v = (p.over || p.mag > 0x7FFF'FFFFull) ? 0x7FFF'FFFFu : static_cast<std::uint32_t>(p.mag);
    }
    return Scan{v, p.used};
}

[[nodiscard]] constexpr int scan_atoi(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && is_c_space(s[i]))
        ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        ++i;
    }
    long long acc = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        if (acc < 100000000000ll)
            acc = acc * 10 + (s[i] - '0');
        ++i;
    }
    return static_cast<int>(neg ? -acc : acc);
}

}
