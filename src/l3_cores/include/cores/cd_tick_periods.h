// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

namespace mister::cores {

struct CdTickPeriods {
    std::chrono::microseconds idle;
    std::chrono::microseconds stream;

    std::chrono::microseconds caller;
    std::uint8_t speeds;
    std::chrono::microseconds speed[4];
};

}
