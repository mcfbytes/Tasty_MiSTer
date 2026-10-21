// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace mister::cores::mra {

struct FillRun {
    std::uint32_t at = 0;
    std::uint32_t len = 0;
    std::vector<std::uint8_t> pattern;
};

}
