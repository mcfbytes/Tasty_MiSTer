// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/fact.h"

namespace mister::hal {

enum class DdrCoherency : std::uint8_t { NonCoherent, IoCoherent };
enum class WriteCombine : std::uint8_t { Forbidden, Allowed };

struct MemoryModel {
    Fact<DdrCoherency> fpga_ddr;
    Fact<WriteCombine> write_combine;
};

}  // namespace mister::hal
