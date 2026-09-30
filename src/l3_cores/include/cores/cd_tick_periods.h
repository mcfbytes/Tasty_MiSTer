// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct CdTickPeriods {
    std::uint32_t idle_us;
    std::uint32_t stream_us;

    std::uint32_t caller_us;
    std::uint8_t speeds;
    std::uint32_t speed_us[4];
};

}  // namespace mister::cores
