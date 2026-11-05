// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct Progress {
    std::uint16_t done = 0;
    std::uint16_t total = 0;
    friend constexpr bool operator==(Progress, Progress) = default;
};

}
