// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace mister::infra {

inline void json_name_not_clean() noexcept {}

class JsonName {
public:
    consteval JsonName(const char* s) : s_(s) {
        for (const char* p = s; *p != '\0'; ++p) {
            if (*p < 0x20 || *p > 0x7e || *p == '"' || *p == '\\') json_name_not_clean();
        }
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept { return s_; }

private:
    std::string_view s_;
};

template <std::size_t N>
[[nodiscard]] consteval bool json_clean_names(const std::array<const char*, N>& names) {
    for (const char* n : names) {
        if (n == nullptr) return false;
        for (const char* p = n; *p != '\0'; ++p) {
            if (*p < 0x20 || *p > 0x7e || *p == '"' || *p == '\\') return false;
        }
    }
    return true;
}

template <std::size_t N>
[[nodiscard]] consteval std::size_t json_longest(const std::array<const char*, N>& names) {
    std::size_t best = 0;
    for (const char* n : names) {
        const std::size_t len = std::string_view(n).size();
        if (len > best) best = len;
    }
    return best;
}

}  // namespace mister::infra
