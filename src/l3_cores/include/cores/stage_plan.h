// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/fixed_str.h"
#include "proto/types.h"

namespace mister::cores {

struct StagePlan {
    proto::FileSize disc_size_bytes{};
    bool data_first_track = false;
    FixedStr<11, StrFit::Clip> game_id{};
};

}
