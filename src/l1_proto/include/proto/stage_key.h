// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/save_stage_types.h"

namespace mister::proto {

struct StageKey {
    StageEpoch epoch{};
    SaveStageSlot slot{};
    friend constexpr bool operator==(const StageKey&, const StageKey&) = default;
};

}
