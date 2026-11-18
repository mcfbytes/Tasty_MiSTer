// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/error.h"
#include "os/raw_monotonic_clock.h"
#include "proto/save_stage_types.h"
#include "proto/stage_key.h"

namespace mister::proto {

enum class StageState : std::uint8_t { Free, Loading, Live, Retiring, Broken };

struct SaveStageRow {
    StageKey key{};
    StageState state = StageState::Free;
    bool present = false;
    StageRevision rev{};
    StageRevision saved{};
    os::RawMonotonicClock::time_point last_write{};
    StageTally commits{};
    StageTally skipped_identical{};
    StageTally faults{};
    std::optional<Error> last_fault{};
    StageTally declines{};
};

static_assert(std::is_trivially_copyable_v<SaveStageRow>);

}
