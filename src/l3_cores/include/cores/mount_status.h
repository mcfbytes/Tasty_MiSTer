// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/mount_state.h"
#include "cores/stage_plan.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

struct MountStatus {
    TASTY_SEAT_EXEMPT(const_shared);
    proto::ActKey generation{};
    MountState state = MountState::Pending;
    StagePlan plan{};
};

enum class MountVerdict : std::uint8_t { Done, Failed };

constexpr void set_verdict(MountStatus& level, MountVerdict v) noexcept {
    level.state = v == MountVerdict::Done ? MountState::Done : MountState::Failed;
}

}
