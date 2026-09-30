// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "infra/fixed_str.h"

namespace mister::app {

struct GameId {
    static constexpr std::size_t kNameCap = 64;
    static constexpr std::size_t kBodyCap = 96;
    using Name = FixedStr<kNameCap, StrFit::Clip>;
    using Body = FixedStr<kBodyCap, StrFit::Clip>;

    Name file{};
    Name serial{};
    std::uint32_t crc = 0;
    bool enabled = false;

    [[nodiscard]] bool writes() const noexcept {
        if (!enabled || file.empty()) return false;
        std::string_view f = file.view();
        const std::size_t slash = f.rfind('/');
        if (slash != std::string_view::npos) f = f.substr(slash + 1);
        const auto ieq4 = [](std::string_view a, const char* b) noexcept {
            for (std::size_t i = 0; i < 4; ++i) {
                char c = a[i];
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                if (c != b[i]) return false;
            }
            return true;
        };
        if (f.size() >= 4 && ieq4(f, "boot")) return false;
        for (std::size_t i = 0; i + 4 <= f.size(); ++i) {
            if (ieq4(f.substr(i, 4), "bios")) return false;
        }
        return true;
    }

    [[nodiscard]] Body body() const noexcept {
        char buf[Body::kBufSize] = {};
        std::size_t n = 0;
        const auto room = [&]() noexcept { return sizeof buf - n; };
        if (crc != 0) {
            const int w = std::snprintf(buf + n, room(), "CRC32: %08X\n", crc);
            if (w > 0)
                n +=
                    static_cast<std::size_t>(w) < room() ? static_cast<std::size_t>(w) : room() - 1;
        }
        if (!serial.empty()) {
            const int w = std::snprintf(buf + n, room(), "Serial: %s\n", serial.c_str());
            if (w > 0)
                n +=
                    static_cast<std::size_t>(w) < room() ? static_cast<std::size_t>(w) : room() - 1;
        }
        if (n == 0) {
            const int w = std::snprintf(buf, sizeof buf, "# No game ID available\n");
            if (w > 0) n = static_cast<std::size_t>(w);
        }
        Body out{};
        (void)out.assign(std::string_view{buf, n});
        return out;
    }
};

}  // namespace mister::app
