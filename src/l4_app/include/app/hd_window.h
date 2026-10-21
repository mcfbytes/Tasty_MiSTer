// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/page_description.h"

namespace mister::app {

struct HdWindow {
    std::uint16_t first;
    std::uint8_t count;
};

[[nodiscard]] constexpr HdWindow hd_window(
    std::uint16_t selected,
    std::uint16_t total,
    std::uint16_t prev_first,
    std::uint16_t height = static_cast<std::uint16_t>(PageDescription::kMaxRows)
) noexcept {
    if (total == 0 || height == 0)
        return HdWindow{0, 0};
    const std::uint16_t sel = selected >= total ? static_cast<std::uint16_t>(total - 1) : selected;
    const std::uint16_t count = total < height ? total : height;
    const std::uint16_t max_first = static_cast<std::uint16_t>(total - count);
    std::uint16_t first = prev_first > max_first ? max_first : prev_first;
    if (sel < first) {
        first = sel;
    } else if (sel >= static_cast<std::uint16_t>(first + count)) {
        first = static_cast<std::uint16_t>(sel - count + 1);
    }
    return HdWindow{first, static_cast<std::uint8_t>(count)};
}

}
