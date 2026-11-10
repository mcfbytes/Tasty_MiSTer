// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

namespace mister::infra {

[[nodiscard]] constexpr std::string_view utf8_prefix(std::string_view s, std::size_t cap) noexcept {
    if (s.size() <= cap)
        return s;
    std::size_t n = cap;
    constexpr unsigned kContinuationMask = 0xC0u;
    constexpr unsigned kContinuationTag = 0x80u;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & kContinuationMask) == kContinuationTag)
        --n;
    return s.substr(0, n);
}

}
