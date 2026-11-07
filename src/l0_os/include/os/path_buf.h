// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <span>

namespace mister::os {

inline constexpr std::size_t kUioNodeNameBuf = 48;

inline constexpr std::size_t kUioSysfsPathBuf = 128;

inline constexpr std::size_t kUioDevPathBuf = 160;

inline constexpr std::size_t kVtTtyPathBuf = 16;

inline constexpr std::size_t kI2cDevPathBuf = 32;

[[gnu::format(printf, 2, 3)]]
inline bool format_path(std::span<char> out, const char* fmt, ...) noexcept {
    if (out.empty())
        return false;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(out.data(), out.size(), fmt, ap);
    va_end(ap);
    return n >= 0 && static_cast<std::size_t>(n) < out.size();
}

}
