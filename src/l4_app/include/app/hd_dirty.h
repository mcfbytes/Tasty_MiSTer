// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/hd_rect.h"

namespace mister::app {

struct HdDirty {

    static constexpr std::size_t kMaxRects = 6;
    std::array<HdRect, kMaxRects> rects{};
    std::uint8_t count = 0;
};

}
