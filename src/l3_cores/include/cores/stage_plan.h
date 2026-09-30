// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/fixed_str.h"

namespace mister::cores {

struct StagePlan {
    std::uint64_t disc_size_bytes = 0;
    bool data_first_track = false;
    FixedStr<11, StrFit::Clip> game_id{};
};

}  // namespace mister::cores
