// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cores/dip_row.h"

namespace mister::cores::mra {

inline constexpr std::size_t kMaxDipRows = 63;

struct SwitchTable {
    DipWord dip_def{};
    std::vector<DipRow> rows;
};

}
