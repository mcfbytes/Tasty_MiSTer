// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <format>
#include <string_view>

#include "infra/ascii.h"
#include "infra/fixed_str.h"
#include "infra/path_parts.h"
#include "svc/types.h"

namespace mister::app {

struct GameId {
    static constexpr std::size_t kNameCap = 64;
    static constexpr std::size_t kBodyCap = 96;
    using Name = FixedStr<kNameCap, StrFit::Clip>;
    using Body = FixedStr<kBodyCap, StrFit::Clip>;

    Name file{};
    Name serial{};
    svc::Crc32 crc{};
    bool enabled = false;

    [[nodiscard]] bool writes() const noexcept {
        if (!enabled || file.empty())
            return false;
        std::string_view f = infra::basename(file.view());
        if (infra::istarts_with(f, "boot"))
            return false;
        for (std::size_t i = 0; i + 4 <= f.size(); ++i) {
            if (infra::ieq(f.substr(i, 4), "bios"))
                return false;
        }
        return true;
    }

    [[nodiscard]] Body body() const noexcept {
        Body out{};
        if (crc.declared()) {

            char line[Body::kBufSize] = {};
            const auto be = crc.to_be_bytes();
            std::format_to_n(
                line,
                Body::kCapacity,
                "CRC32: {:02X}{:02X}{:02X}{:02X}\n",
                be[0],
                be[1],
                be[2],
                be[3]
            );
            (void)out.append(line);
        }
        if (!serial.empty()) {
            char line[Body::kBufSize] = {};
            std::format_to_n(line, Body::kCapacity, "Serial: {}\n", serial.view());
            (void)out.append(line);
        }
        if (out.empty())
            (void)out.assign("# No game ID available\n");
        return out;
    }
};

}
