// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/stock_scan.h"

namespace mister::cores::zx81 {

struct ColourCode {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::size_t count() noexcept {
        constexpr std::size_t kCodes = 128;
        return kCodes;
    }

    [[nodiscard]] static constexpr std::optional<ColourCode> of_text(
        std::string_view decimal
    ) noexcept {
        constexpr int kLast = 127;
        const int code = stock_scan::scan_atoi(decimal);
        if (code < 0 || code > kLast)
            return std::nullopt;
        return ColourCode{static_cast<std::uint8_t>(code)};
    }

    friend constexpr bool operator==(ColourCode, ColourCode) = default;
};

}
