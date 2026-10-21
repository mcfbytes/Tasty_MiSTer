// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct ManifestCounts {
    std::uint32_t sent = 0;
    std::uint32_t discarded = 0;
    std::uint32_t ddr_refusals = 0;
    std::uint32_t staged = 0;
    std::uint32_t repeat_refusals = 0;
    std::uint32_t nvram_loads = 0;
};

}
