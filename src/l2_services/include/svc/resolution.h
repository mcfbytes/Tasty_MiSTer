// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct Resolution {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend constexpr bool operator==(Resolution, Resolution) = default;
};

}
