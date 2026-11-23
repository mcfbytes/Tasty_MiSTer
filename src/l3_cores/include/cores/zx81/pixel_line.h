// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/stock_scan.h"

namespace mister::cores::zx81 {

struct PixelLine {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::size_t count() noexcept {
        constexpr std::size_t kLines = 8;
        return kLines;
    }

    [[nodiscard]] static constexpr std::optional<PixelLine> of_text(
        std::string_view decimal
    ) noexcept {
        constexpr int kLast = 7;
        const int index = stock_scan::scan_atoi(decimal);
        if (index < 0 || index > kLast)
            return std::nullopt;
        return PixelLine{static_cast<std::uint8_t>(index)};
    }

    friend constexpr bool operator==(PixelLine, PixelLine) = default;
};

}
