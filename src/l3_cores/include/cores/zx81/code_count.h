// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/stock_scan.h"

namespace mister::cores::zx81 {

struct CodeCount {
    std::uint8_t v = 1;

    [[nodiscard]] static constexpr CodeCount of_text(std::string_view decimal) noexcept {
        constexpr int kMost = 128;
        const int quantity = stock_scan::scan_atoi(decimal);
        if (quantity < 1)
            return CodeCount{};
        return CodeCount{static_cast<std::uint8_t>(quantity > kMost ? kMost : quantity)};
    }

    friend constexpr bool operator==(CodeCount, CodeCount) = default;
};

static_assert(
    CodeCount::of_text("-3") == CodeCount{1} && CodeCount::of_text("500") == CodeCount{128}
);

}
