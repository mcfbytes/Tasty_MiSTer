// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct TextSpan {
    std::uint16_t off = 0;
    std::uint16_t len = 0;
    friend constexpr bool operator==(TextSpan, TextSpan) = default;
};

}
