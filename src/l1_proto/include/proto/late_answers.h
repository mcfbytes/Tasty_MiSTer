// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct LateAnswers {
    std::uint32_t count = 0;
    std::uint32_t max_us = 0;
};

}  // namespace mister::proto
