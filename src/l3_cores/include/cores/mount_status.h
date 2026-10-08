// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/staging_core.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores {

struct MountStatus {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t generation = 0;
    MountState state = MountState::Pending;
    bool data_first_track = false;
    std::uint64_t disc_size_bytes = 0;
    FixedStr<11, StrFit::Clip> game_id{};
};

enum class MountVerdict : std::uint8_t { Done, Failed };

constexpr void set_verdict(MountStatus& level, MountVerdict v) noexcept {
    level.state = v == MountVerdict::Done ? MountState::Done : MountState::Failed;
}

}  // namespace mister::cores
