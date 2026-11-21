// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::neogeo {

struct RomsetConfig {
    std::uint32_t ms5p = 0;
    std::uint32_t xram = 0;
    std::uint32_t hw_type = 0;
    std::uint32_t cmc = 0;
    std::uint32_t pvc = 0;
    std::uint32_t sma = 0;
    std::uint32_t use_pcm = 1;
    std::uint32_t rom_wait = 0;
    std::uint32_t p_wait = 0;
};

}
