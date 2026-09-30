// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct AnalogXy {
    std::int32_t x = 0;
    std::int32_t y = 0;
    friend constexpr bool operator==(AnalogXy, AnalogXy) = default;
};

}  // namespace mister::svc
