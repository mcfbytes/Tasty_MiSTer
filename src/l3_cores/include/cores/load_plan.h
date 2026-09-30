// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "cores/transfer_row.h"

namespace mister::cores {

inline constexpr std::size_t kMaxTransferRows = 32;

struct PlanFrame {
    bool hold_reset = false;
    bool release_reset = false;
    bool refused = false;
    std::string_view save{};
};

struct LoadPlan {
    std::array<TransferRow, kMaxTransferRows> rows{};
    std::size_t count = 0;
    PlanFrame frame{};
};

}  // namespace mister::cores
