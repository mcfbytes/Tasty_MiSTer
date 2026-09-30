// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct AxisCal {
    std::int32_t min = 0;
    std::int32_t max = 0;
    std::int32_t flat = 0;
    std::int32_t fuzz = 0;
    std::int32_t seen_max = 0;
};

}  // namespace mister::svc
